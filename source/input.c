// Touch + botones de la Vita -> eventos del motor, replicando -[EAGLView touchesBegan/Moved/Ended]:
// primero el pad virtual (VP_*); si no consumió el toque, handleCletEvent + EVENT_POINT_*.
// Coordenadas del juego: apaisado 480x320, como se ve en pantalla (gles.c lo dibuja en
// 816x544 centrado).
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <string.h>

#include "hle.h"
#include "macho.h"

#define TOUCH_PRESS 0x80000001
#define TOUCH_MOVE 0x80000002
#define TOUCH_RELEASE 0x80000004
#define KEY_PRESS 2
#define KEY_RELEASE 3

#define DST_W 816
#define DST_X 72

static void *vp_touch, *vp_move, *vp_release, *clet_event, *ev_press, *ev_move, *ev_release;
static uint8_t *sleep_key;

static struct {
    int id, x, y;
} prev[SCE_TOUCH_MAX_REPORT];
static int nprev;
static uint32_t prev_buttons;

// ponytail: códigos de tecla WIPI supuestos (-1..-4 flechas, -5 OK, -6/-7 soft, -8 borrar); el
// juego iOS es táctil y quizás ignore teclas. Si no responden, mapear botones a toques fijos.
static const struct {
    uint32_t button;
    int key;
} keymap[] = {
    {SCE_CTRL_UP, -1}, {SCE_CTRL_DOWN, -2}, {SCE_CTRL_LEFT, -3}, {SCE_CTRL_RIGHT, -4},
    {SCE_CTRL_CROSS, -5}, {SCE_CTRL_START, -5},
    {SCE_CTRL_TRIANGLE, -6}, {SCE_CTRL_SELECT, -6},
    {SCE_CTRL_SQUARE, -7}, {SCE_CTRL_CIRCLE, -8},
};

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
}

static void to_game(const SceTouchReport *r, int *x, int *y) {
    int gx = (r->x / 2 - DST_X) * 480 / DST_W, gy = r->y / 2 * 320 / 544;
    *x = gx < 0 ? 0 : gx > 479 ? 479 : gx;
    *y = gy < 0 ? 0 : gy > 319 ? 319 : gy;
}

static void point(void *vp, uint32_t type, void *ev, int x, int y) {
    if (!call_guest(vp, x, y, 0, 0)) {
        call_guest(clet_event, type, x, y, 0);
        call_guest(ev, x, y, 0, 0);
    }
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

    uint32_t changed = buttons ^ prev_buttons;
    for (unsigned k = 0; k < sizeof(keymap) / sizeof(keymap[0]); k++)
        if (changed & keymap[k].button)
            call_guest(clet_event, (buttons & keymap[k].button) ? KEY_PRESS : KEY_RELEASE, keymap[k].key, 0, 0);
    prev_buttons = buttons;
}
