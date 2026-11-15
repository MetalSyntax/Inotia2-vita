// Touch + botones de la Vita -> eventos del motor, replicando -[EAGLView touchesBegan/Moved/Ended]:
// primero el pad virtual (VP_*); si no consumió el toque, handleCletEvent + EVENT_POINT_*.
// Coordenadas del juego: apaisado 480x320, como se ve en pantalla (gles.c lo dibuja con
// viewport dinámico según el modo de aspecto configurado en graphics_enhancer).
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>

#include "controls.h"
#include "graphics_enhancer.h"
#include "hle.h"
#include "macho.h"

#define TOUCH_PRESS 0x80000001
#define TOUCH_MOVE 0x80000002
#define TOUCH_RELEASE 0x80000004
#define KEY_PRESS 2
#define KEY_RELEASE 3

static void *vp_touch, *vp_move, *vp_release, *clet_event, *ev_press, *ev_move, *ev_release;
static uint8_t *sleep_key;
static void *fn_get_uiview_open;
static void *fn_set_uiview_open;
static void *fn_vp_ui_resume;
static void *fn_change_game_state;
static void *fn_do_play_sound;
static uint8_t *g_ui_view_ptr;

static int is_ingame_menu_open(void) {
    if (g_ui_view_ptr && *g_ui_view_ptr != 0)
        return 1;
    if (fn_get_uiview_open && call_guest(fn_get_uiview_open, 0, 0, 0, 0) != 0)
        return 1;
    return 0;
}

static struct {
    int id, x, y;
} prev[SCE_TOUCH_MAX_REPORT];
static int nprev;
static uint32_t prev_buttons;

// Códigos WIPI para navegación de menús con cruceta/OK
static struct {
    uint32_t button;
    int key;
} keymap[] = {
    {SCE_CTRL_UP, -1}, {SCE_CTRL_DOWN, -2}, {SCE_CTRL_LEFT, -3}, {SCE_CTRL_RIGHT, -4},
    {SCE_CTRL_CROSS, -5},
};

// Botones físicos -> toque sobre un botón del pad virtual. La posición se lee del pool de objetos
// del juego (_VP_objList, 30 x 0x88 bytes, lo llena make_vpbutton según Config_GetControl), así
// sirve para los dos modos de control. Campos: +0 estado (1 = en uso), +0x1c/+0x30 x/y del área
// táctil, +0x44/+0x58 ancho/alto, +0x80 tipo.
#define VP_OBJS 30
#define VP_OBJ_SIZE 0x88

// Acción (controls.h) -> tipo de botón del pad virtual; el botón físico sale de controls.txt.
static const int vtypes[ACT_COUNT] = {
    -5,   // ataque (solo en el modo con pad)
    0x34, // habilidad 1
    0x35, // habilidad 2
    0x36, // habilidad 3
    0x37, // habilidad 4
    0x38, // ranura de objeto
    0x64, // dragón: menú
    0x65, // icono sup. derecho
};
static struct {
    int x, y;
} vpressed[ACT_COUNT];
// START/SELECT tocan al soltar (START+SELECT abre el menú del port sin tocar nada): toque
// diferido que se suelta 100 ms después, para que el juego lo vea en algún tick.
#define COMBO (SCE_CTRL_START | SCE_CTRL_SELECT)
static int combo_used;
static uint64_t tap_until[ACT_COUNT];
static uint8_t *vp_objs;

static int vp_find(int type, int *x, int *y) {
    for (int i = 0; vp_objs && i < VP_OBJS; i++) {
        const int32_t *o = (const int32_t *)(vp_objs + i * VP_OBJ_SIZE);
        if (o[0] == 1 && o[0x80 / 4] == type) {
            *x = o[0x1c / 4] + o[0x44 / 4] / 2;
            *y = o[0x30 / 4] + o[0x58 / 4] / 2;
            return 1;
        }
    }
    return 0;
}

void input_init(void) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    vp_touch = (void *)macho_sym("_VP_touchesProcessTouch");
    vp_move = (void *)macho_sym("_VP_touchesProcessMove");
    vp_release = (void *)macho_sym("_VP_touchesProcessRelese");
    clet_event = (void *)macho_sym("_handleCletEvent");
    ev_press = (void *)macho_sym("_EVENT_POINT_PRESS");
    ev_move = (void *)macho_sym("_EVENT_POINT_MOVE");
    ev_release = (void *)macho_sym("_EVENT_POINT_RELEASE");
    sleep_key = (uint8_t *)macho_sym("_g_SleepKey");
    vp_objs = (uint8_t *)macho_sym("_VP_objList");
    fn_get_uiview_open = (void *)macho_sym("_GetUIViewOpen");
    fn_set_uiview_open = (void *)macho_sym("_SetUIViewOpen");
    fn_vp_ui_resume = (void *)macho_sym("_VP_UIresume");
    fn_change_game_state = (void *)macho_sym("_ChangeGameState");
    fn_do_play_sound = (void *)macho_sym("_DoPlaySound");
    g_ui_view_ptr = (uint8_t *)macho_sym("_g_uiView");
    for (int k = 0; k < ACT_COUNT; k++)
        vpressed[k].x = -1;
}

static void to_game(const SceTouchReport *r, int *x, int *y) {
    float rx, ry, rw, rh;
    graphics_enhancer_get_render_bounds(&rx, &ry, &rw, &rh);
    float tx = (float)(r->x / 2);
    float ty = (float)(r->y / 2);
    float gx = (tx - rx) * 480.0f / rw;
    float gy = (ty - ry) * 320.0f / rh;
    *x = gx < 0.0f ? 0 : gx > 479.0f ? 479 : (int)gx;
    *y = gy < 0.0f ? 0 : gy > 319.0f ? 319 : (int)gy;
}

static void point(void *vp, uint32_t type, void *ev, int x, int y) {
    if (!call_guest(vp, x, y, 0, 0)) {
        call_guest(clet_event, type, x, y, 0);
        call_guest(ev, x, y, 0, 0);
    }
}

static uint32_t read_buttons(void) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t buttons = pad.buttons;
    if (pad.lx < 64)
        buttons |= SCE_CTRL_LEFT;
    else if (pad.lx > 192)
        buttons |= SCE_CTRL_RIGHT;
    if (pad.ly < 64)
        buttons |= SCE_CTRL_UP;
    else if (pad.ly > 192)
        buttons |= SCE_CTRL_DOWN;
    return buttons;
}

void input_poll(void) {
    SceTouchData t;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) < 1)
        t.reportNum = 0;
    int live = !*sleep_key; // en iOS los toques se ignoran mientras g_SleepKey != 0
    int moved = 0;
    for (int i = 0; i < (int)t.reportNum; i++) {
        int x, y, j = 0;
        to_game(&t.report[i], &x, &y);
        while (j < nprev && prev[j].id != t.report[i].id)
            j++;
        if (j == nprev) {
            if (live)
                point(vp_touch, TOUCH_PRESS, ev_press, x, y);
        } else if ((prev[j].x != x || prev[j].y != y) && !moved) {
            moved = 1; // touchesMoved de iOS usa un solo toque (anyObject)
            if (live)
                point(vp_move, TOUCH_MOVE, ev_move, x, y);
        }
    }
    for (int j = 0; j < nprev; j++) {
        int i = 0;
        while (i < (int)t.reportNum && t.report[i].id != prev[j].id)
            i++;
        if (i == (int)t.reportNum && live)
            point(vp_release, TOUCH_RELEASE, ev_release, prev[j].x, prev[j].y);
    }
    nprev = t.reportNum;
    for (int i = 0; i < nprev; i++) {
        prev[i].id = t.report[i].id;
        to_game(&t.report[i], &prev[i].x, &prev[i].y);
    }

    uint32_t buttons = read_buttons();
    uint32_t changed = buttons ^ prev_buttons;
    uint32_t pressed = buttons & changed;

    // Start + Select abre el menú de opciones del port
    if ((buttons & COMBO) == COMBO && !combo_used) {
        combo_used = 1;
        prev_buttons = buttons;
        controls_menu_open();
        return;
    }

    // Hotkeys con gatillos L y R:
    // Mantener L + R + TRIANGLE -> Rotar filtros gráficos
    // Mantener L + R + SQUARE   -> Rotar modo de relación de aspecto
    // Soltar L + R (sin triángulo ni cuadrado) -> Ocultar/mostrar botones virtuales (opacidad 0%)
    static int lr_held = 0;
    static int lr_sub_used = 0;
    int lr_both = (buttons & SCE_CTRL_LTRIGGER) && (buttons & SCE_CTRL_RTRIGGER);
    int prev_lr_both = (prev_buttons & SCE_CTRL_LTRIGGER) && (prev_buttons & SCE_CTRL_RTRIGGER);

    if (lr_both) {
        lr_held = 1;
        // Si L o R tenían una pulsación en curso al entrar al combo, cancelarla para evitar toques residuales
        for (int k = 0; k < ACT_COUNT; k++) {
            uint32_t b = controls_binding(k);
            if ((b & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) && vpressed[k].x >= 0) {
                if (live)
                    point(vp_release, TOUCH_RELEASE, ev_release, vpressed[k].x, vpressed[k].y);
                vpressed[k].x = -1;
            }
        }
        if (pressed & SCE_CTRL_TRIANGLE) {
            combo_used = 1;
            lr_sub_used = 1;
            graphics_enhancer_cycle_filter();
            controls_save();
        }
        if (pressed & SCE_CTRL_SQUARE) {
            combo_used = 1;
            lr_sub_used = 1;
            graphics_enhancer_cycle_aspect();
            controls_save();
        }
    } else if (prev_lr_both && lr_held) {
        if (!lr_sub_used) {
            combo_used = 1;
            controls_toggle_vbuttons();
        }
        lr_held = 0;
        lr_sub_used = 0;
    }

    // Select + R -> Alternar modo FPS (20 FPS ORIGINAL <-> 30 FPS <-> 60 FPS)
    if ((buttons & SCE_CTRL_SELECT) && (pressed & SCE_CTRL_RTRIGGER)) {
        combo_used = 1;
        controls_toggle_fps();
    }

    keymap[4].button = controls_binding(ACT_ATTACK); // OK de los menús del juego = ataque
    for (unsigned k = 0; k < sizeof(keymap) / sizeof(keymap[0]); k++)
        if (changed & keymap[k].button)
            call_guest(clet_event, (buttons & keymap[k].button) ? KEY_PRESS : KEY_RELEASE, keymap[k].key, 0, 0);

    uint64_t now = sceKernelGetProcessTimeWide();
    for (int k = 0; k < ACT_COUNT; k++) {
        uint32_t b = controls_binding(k);
        // Si L y R están pulsados juntos, no disparar sus acciones de juego individuales
        if (lr_both && (b & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)))
            continue;
        int tap = (b & COMBO) != 0;
        if (tap && tap_until[k] && now >= tap_until[k]) { // fin del toque diferido
            tap_until[k] = 0;
            if (vpressed[k].x >= 0 && live)
                point(vp_release, TOUCH_RELEASE, ev_release, vpressed[k].x, vpressed[k].y);
            vpressed[k].x = -1;
        }
        if (!(changed & b))
            continue;
        int down = (buttons & b) != 0;
        if (tap ? (!down && !combo_used && !tap_until[k]) : down) {
            vpressed[k].x = -1;
            if (k == ACT_MENU && is_ingame_menu_open()) {
                // El menú del juego está abierto -> cerrarlo y volver al juego
                if (fn_vp_ui_resume)
                    call_guest(fn_vp_ui_resume, 0, 0, 0, 0);
                if (fn_set_uiview_open)
                    call_guest(fn_set_uiview_open, 0, 0, 0, 0);
                if (fn_change_game_state)
                    call_guest(fn_change_game_state, 2, 0, 0, 0);
                if (fn_do_play_sound)
                    call_guest(fn_do_play_sound, 4, 0, 0, 0);
            } else if (live) {
                if (vp_find(vtypes[k], &vpressed[k].x, &vpressed[k].y)) {
                    point(vp_touch, TOUCH_PRESS, ev_press, vpressed[k].x, vpressed[k].y);
                }
            }
            if (tap)
                tap_until[k] = now + 100000;
        } else if (!tap && vpressed[k].x >= 0) {
            if (live)
                point(vp_release, TOUCH_RELEASE, ev_release, vpressed[k].x, vpressed[k].y);
            vpressed[k].x = -1;
        }
    }
    if (!(buttons & (COMBO | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)))
        combo_used = 0;
    prev_buttons = buttons;
}

// Con el menú del port abierto: solo los flancos de presión van al menú.
int input_menu_poll(void) {
    uint32_t buttons = read_buttons(), pressed = buttons & ~prev_buttons;
    prev_buttons = buttons;
    return controls_menu_update(pressed);
}
