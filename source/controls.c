// Botones reasignables + menú del port (START+SELECT, el juego en pausa), como Zenonia 2 y Destinia.
// controls.txt (a mano también): "ACCION = BOTON", "LANG = EN|ES|PT", "FPS = 20|30|60", "VBUTTONS = SHOWN|HIDDEN", "FILTER = N", "ASPECT = N".
#include <ctype.h>
#include <kubridge.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "controls.h"
#include "gles.h"
#include "graphics_enhancer.h"
#include "hle.h"
#include "log.h"
#include "macho.h"

#define CONTROLS_PATH DATA_PATH "controls.txt"

void ui_text_rgba(uint32_t *px, int pw, int ph, int x, int y, int size, uint32_t color, const char *s); // objc.c

static const struct {
    const char *name;
    uint32_t bit;
} btn_names[] = {
    {"CROSS", SCE_CTRL_CROSS}, {"CIRCLE", SCE_CTRL_CIRCLE}, {"SQUARE", SCE_CTRL_SQUARE},
    {"TRIANGLE", SCE_CTRL_TRIANGLE}, {"L", SCE_CTRL_LTRIGGER}, {"R", SCE_CTRL_RTRIGGER},
    {"START", SCE_CTRL_START}, {"SELECT", SCE_CTRL_SELECT},
};
#define BTN_COUNT (int)(sizeof(btn_names) / sizeof(btn_names[0]))

static const struct {
    const char *key, *label;
    uint32_t def;
} actions[ACT_COUNT] = {
    {"ATTACK", "Attack / OK", SCE_CTRL_CROSS},  {"SKILL1", "Skill 1", SCE_CTRL_SQUARE},
    {"SKILL2", "Skill 2", SCE_CTRL_TRIANGLE},   {"SKILL3", "Skill 3", SCE_CTRL_CIRCLE},
    {"SKILL4", "Skill 4", SCE_CTRL_RTRIGGER},   {"ITEM", "Quick item", SCE_CTRL_LTRIGGER},
    {"MENU", "Menu (dragon)", SCE_CTRL_START},  {"MAP", "Map / top-right icon", SCE_CTRL_SELECT},
};
static uint32_t binding[ACT_COUNT];

static const struct {
    const char *key, *label, *path;
} langs[] = {
    {"EN", "ENGLISH", NULL},
    {"ES", "ESPAÑOL", DATA_PATH "lang/spa/"},
    {"PT", "PORTUGUÊS", DATA_PATH "lang/por/"},
};
#define LANG_COUNT (int)(sizeof(langs) / sizeof(langs[0]))
static int lang, loaded_lang;
static int g_fps_mode = 0;         // 0 = 20 FPS (ORIGINAL), 1 = 30 FPS (Forced), 2 = 60 FPS (Forced)
static int g_vbuttons_visible = 0; // 0 = ocultos (opacidad 0%), 1 = visibles

const char *controls_lang_path(void) { return langs[loaded_lang].path; }
uint32_t controls_binding(int a) { return binding[a]; }

int controls_get_fps_mode(void) {
    return g_fps_mode;
}

void controls_set_fps_mode(int mode) {
    if (mode < 0 || mode > 2)
        mode = 0;
    g_fps_mode = mode;
    gles_set_fps_mode(g_fps_mode);
}

void controls_toggle_fps(void) {
    controls_set_fps_mode((g_fps_mode + 1) % 3);
    controls_save();
}

int controls_get_vbuttons(void) {
    return g_vbuttons_visible;
}

void controls_set_vbuttons(int visible) {
    g_vbuttons_visible = visible ? 1 : 0;
    uintptr_t fn = macho_sym("_VP_drawObjects");
    if (fn) {
        void *addr = (void *)(fn & ~1);
        uint16_t insn = g_vbuttons_visible ? 0xb580 : 0x4770; // push {r7, lr} : bx lr
        kuKernelCpuUnrestrictedMemcpy(addr, &insn, sizeof(insn));
        kuKernelFlushCaches(addr, sizeof(insn));
    }
}

void controls_toggle_vbuttons(void) {
    controls_set_vbuttons(!g_vbuttons_visible);
    controls_save();
}

static int lang_installed(int l) {
    char p[256];
    snprintf(p, sizeof(p), "%sstrdata.dat", langs[l].path ? langs[l].path : BUNDLE_PATH);
    return sceIoGetstat(p, &(SceIoStat){0}) >= 0;
}

static void set_defaults(void) {
    for (int a = 0; a < ACT_COUNT; a++)
        binding[a] = actions[a].def;
    controls_set_fps_mode(0);
    controls_set_vbuttons(0);
    graphics_enhancer_set_filter(FILTER_SHARP_PIXEL_ART);
    graphics_enhancer_set_aspect(ASPECT_RATIO_FULLSCREEN);
}

static const char *btn_name(uint32_t bit) {
    for (int b = 0; b < BTN_COUNT; b++)
        if (btn_names[b].bit == bit)
            return btn_names[b].name;
    return "NONE";
}

void controls_save(void) {
    FILE *f = fopen(CONTROLS_PATH, "w");
    if (!f)
        return l_error("no se pudo escribir " CONTROLS_PATH);
    for (int a = 0; a < ACT_COUNT; a++)
        fprintf(f, "%s = %s\n", actions[a].key, btn_name(binding[a]));
    const char *fps_str = (g_fps_mode == 1) ? "30" : (g_fps_mode == 2) ? "60" : "20";
    fprintf(f, "FPS = %s\n", fps_str);
    fprintf(f, "VBUTTONS = %s\n", g_vbuttons_visible ? "SHOWN" : "HIDDEN");
    fprintf(f, "FILTER = %d\n", (int)graphics_enhancer_get_filter());
    fprintf(f, "ASPECT = %d\n", (int)graphics_enhancer_get_aspect());
    fprintf(f, "LANG = %s\n", langs[lang].key);
    fclose(f);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

void controls_init(void) {
    set_defaults();
    graphics_enhancer_init();
    FILE *f = fopen(CONTROLS_PATH, "r");
    if (!f)
        return controls_save(); // se escriben los valores por defecto para poder editarlo a mano
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = trim(line), *val = trim(eq + 1);
        if (!strcasecmp(key, "FPS")) {
            if (!strcasecmp(val, "30"))
                g_fps_mode = 1;
            else if (!strcasecmp(val, "60"))
                g_fps_mode = 2;
            else
                g_fps_mode = 0;
            continue;
        }
        if (!strcasecmp(key, "VBUTTONS")) {
            g_vbuttons_visible = (!strcasecmp(val, "1") || !strcasecmp(val, "SHOWN"));
            continue;
        }
        if (!strcasecmp(key, "FILTER")) {
            graphics_enhancer_set_filter((GraphicsFilterMode)atoi(val));
            continue;
        }
        if (!strcasecmp(key, "ASPECT")) {
            graphics_enhancer_set_aspect((GraphicsAspectMode)atoi(val));
            continue;
        }
        for (int l = 0; l < LANG_COUNT; l++)
            if (!strcasecmp(key, "LANG") && !strcasecmp(val, langs[l].key))
                lang = l;
        for (int a = 0; a < ACT_COUNT; a++)
            if (!strcasecmp(key, actions[a].key)) {
                binding[a] = 0;
                for (int b = 0; b < BTN_COUNT; b++)
                    if (!strcasecmp(val, btn_names[b].name))
                        binding[a] = btn_names[b].bit;
            }
    }
    fclose(f);
    loaded_lang = lang;
    controls_set_fps_mode(g_fps_mode);
    controls_set_vbuttons(g_vbuttons_visible);
    l_info("controls.txt cargado (idioma %s%s, FPS %s, vbuttons %s, filter %s, aspect %s)",
           langs[lang].key, lang_installed(lang) ? "" : ", sin paquete: inglés",
           g_fps_mode == 1 ? "30 FPS (Forced)" : g_fps_mode == 2 ? "60 FPS (Forced)" : "20 FPS (ORIGINAL)",
           g_vbuttons_visible ? "SHOWN" : "HIDDEN (0% opacity)",
           graphics_enhancer_get_filter_name(),
           graphics_enhancer_get_aspect_name());
}

// ---------------------------------------------------------------- menú

enum {
    ROW_LANG = ACT_COUNT,
    ROW_FPS,
    ROW_VBUTTONS,
    ROW_FILTER,
    ROW_ASPECT,
    ROW_DEFAULTS,
    ROW_COUNT
};
static int menu_open, cursor, capture, dirty = 1;

int controls_menu_active(void) { return menu_open; }

void controls_menu_open(void) {
    menu_open = 1;
    capture = 0;
    dirty = 1;
}

int controls_menu_update(uint32_t pressed) {
    if (!menu_open || !pressed)
        return 0;
    dirty = 1;
    if (capture) {
        uint32_t bit = 0;
        for (int b = 0; b < BTN_COUNT && !bit; b++)
            bit = pressed & btn_names[b].bit;
        if (!bit)
            return 0;
        for (int a = 0; a < ACT_COUNT; a++) // un botón por acción: si ya estaba usado, se intercambian
            if (binding[a] == bit)
                binding[a] = binding[cursor];
        binding[cursor] = bit;
        capture = 0;
        return 0;
    }
    if (pressed & SCE_CTRL_UP)
        cursor = (cursor + ROW_COUNT - 1) % ROW_COUNT;
    if (pressed & SCE_CTRL_DOWN)
        cursor = (cursor + 1) % ROW_COUNT;
    if (cursor == ROW_LANG && (pressed & (SCE_CTRL_CROSS | SCE_CTRL_RIGHT)))
        lang = (lang + 1) % LANG_COUNT;
    else if (cursor == ROW_LANG && (pressed & SCE_CTRL_LEFT))
        lang = (lang + LANG_COUNT - 1) % LANG_COUNT;
    else if (cursor == ROW_FPS && (pressed & (SCE_CTRL_CROSS | SCE_CTRL_RIGHT)))
        controls_set_fps_mode((g_fps_mode + 1) % 3);
    else if (cursor == ROW_FPS && (pressed & SCE_CTRL_LEFT))
        controls_set_fps_mode((g_fps_mode + 2) % 3);
    else if (cursor == ROW_VBUTTONS && (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)))
        controls_toggle_vbuttons();
    else if (cursor == ROW_FILTER && (pressed & (SCE_CTRL_CROSS | SCE_CTRL_RIGHT)))
        graphics_enhancer_cycle_filter();
    else if (cursor == ROW_FILTER && (pressed & SCE_CTRL_LEFT)) {
        int f = (graphics_enhancer_get_filter() + FILTER_MAX_COUNT - 1) % FILTER_MAX_COUNT;
        graphics_enhancer_set_filter((GraphicsFilterMode)f);
    }
    else if (cursor == ROW_ASPECT && (pressed & (SCE_CTRL_CROSS | SCE_CTRL_RIGHT)))
        graphics_enhancer_cycle_aspect();
    else if (cursor == ROW_ASPECT && (pressed & SCE_CTRL_LEFT)) {
        int a = (graphics_enhancer_get_aspect() + ASPECT_MAX_COUNT - 1) % ASPECT_MAX_COUNT;
        graphics_enhancer_set_aspect((GraphicsAspectMode)a);
    }
    else if (pressed & SCE_CTRL_CROSS)
        cursor == ROW_DEFAULTS ? set_defaults() : (void)(capture = 1);
    if (!(pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)))
        return 0;
    controls_save();
    menu_open = 0;
    if (lang != loaded_lang) {
        loaded_lang = lang;
        // ponytail: lo que el juego ya tiene armado en pantalla (un diálogo abierto) sigue en el
        // idioma anterior hasta que se vuelve a abrir.
        call_guest((void *)macho_sym("_LoadString"), 0, 0, 0, 0);
        l_info("idioma: %s (textos recargados)", langs[lang].key);
    }
    return 1;
}

#define MENU_W 960
#define MENU_H 544
static uint32_t menu_px[MENU_W * MENU_H];
static GLuint menu_tex;

// ABGR en memoria (GL_RGBA/GL_UNSIGNED_BYTE).
#define C_BG 0xE0201008
#define C_SEL 0xFF603818
#define C_TEXT 0xFFE0E0E0
#define C_DIM 0xFF909090
#define C_HEAD 0xFF40C0FF

static void fill(int y, int h, uint32_t c) {
    for (int i = y * MENU_W; i < (y + h) * MENU_W; i++)
        menu_px[i] = c;
}

static void text(int x, int y, uint32_t c, const char *s) { ui_text_rgba(menu_px, MENU_W, MENU_H, x, y, 18, c, s); }

static void render(void) {
    fill(0, MENU_H, C_BG);
    text(40, 16, C_HEAD, "PS VITA SETTINGS & CONTROLS");
    static const char *fps_labels[] = {"20 FPS (ORIGINAL)", "30 FPS (Mockup)", "60 FPS (Mockup)"};
    for (int r = 0; r < ROW_COUNT; r++) {
        int y = 46 + r * 28;
        if (r == cursor)
            fill(y - 2, 25, C_SEL);
        char val[96];
        if (r < ACT_COUNT) {
            text(40, y, C_TEXT, actions[r].label);
            text(440, y, C_TEXT, r == cursor && capture ? "press a button..." : btn_name(binding[r]));
        } else if (r == ROW_LANG) {
            text(40, y, C_TEXT, "Text language");
            snprintf(val, sizeof(val), "< %s >%s", langs[lang].label,
                     !lang_installed(lang) ? "  (pack not installed)" : lang != loaded_lang ? "  (applies on close)" : "");
            text(440, y, C_TEXT, val);
        } else if (r == ROW_FPS) {
            text(40, y, C_TEXT, "Target Frame Rate");
            snprintf(val, sizeof(val), "< %s >", fps_labels[g_fps_mode]);
            text(440, y, C_TEXT, val);
        } else if (r == ROW_VBUTTONS) {
            text(40, y, C_TEXT, "Virtual buttons (HUD)");
            snprintf(val, sizeof(val), "< %s >", g_vbuttons_visible ? "SHOWN" : "HIDDEN (0% opacity)");
            text(440, y, C_TEXT, val);
        } else if (r == ROW_FILTER) {
            text(40, y, C_TEXT, "Graphics Filter");
            snprintf(val, sizeof(val), "< %s >", graphics_enhancer_get_filter_name());
            text(440, y, C_TEXT, val);
        } else if (r == ROW_ASPECT) {
            text(40, y, C_TEXT, "Aspect Ratio");
            snprintf(val, sizeof(val), "< %s >", graphics_enhancer_get_aspect_name());
            text(440, y, C_TEXT, val);
        } else {
            text(40, y, C_TEXT, "Restore defaults");
        }
    }
    text(40, 468, C_DIM, "X / < >: Change/Toggle   O / START: Save & Close   D-pad/Stick: Navigate");
    text(40, 496, C_DIM, "Hotkeys: Select+R: FPS | L+R: Toggle Buttons | L+R+/\\: Filter | L+R+[]: Aspect");
}

GLuint controls_menu_texture(void) {
    if (!menu_tex) {
        glGenTextures(1, &menu_tex);
        glBindTexture(GL_TEXTURE_2D, menu_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        dirty = 1;
    }
    glBindTexture(GL_TEXTURE_2D, menu_tex);
    if (dirty)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MENU_W, MENU_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, (render(), menu_px));
    dirty = 0;
    return menu_tex;
}
