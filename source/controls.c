// Botones reasignables + menú del port (START+SELECT, el juego en pausa), como Zenonia 2 y Destinia.
// controls.txt (a mano también): "ACCION = BOTON" por línea, "LANG = EN|ES|PT", "FPS = 30|ORIGINAL", "FILTER = N", "ASPECT = N".
// El idioma cambia en vivo al cerrar el menú: LoadString del juego vuelve a leer
// strindex.dat/strdata.dat, que hle_cs.c sirve desde lang/<dir>/ si existen.
#include <ctype.h>
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
static int g_fps_original = 0; // 0 = 30 FPS cap, 1 = 60 FPS / ORIGINAL

const char *controls_lang_path(void) { return langs[loaded_lang].path; }
uint32_t controls_binding(int a) { return binding[a]; }

int controls_get_fps_mode(void) {
    return g_fps_original;
}

void controls_set_fps_mode(int original) {
    g_fps_original = original ? 1 : 0;
    gles_set_fps_mode(g_fps_original);
}

void controls_toggle_fps(void) {
    controls_set_fps_mode(!g_fps_original);
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
    fprintf(f, "FPS = %s\n", g_fps_original ? "ORIGINAL" : "30");
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
            g_fps_original = (!strcasecmp(val, "ORIGINAL") || !strcasecmp(val, "60"));
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
    gles_set_fps_mode(g_fps_original);
    l_info("controls.txt cargado (idioma %s%s, FPS %s, filter %s, aspect %s)",
           langs[lang].key, lang_installed(lang) ? "" : ", sin paquete: inglés",
           g_fps_original ? "60 FPS (ORIGINAL)" : "30 FPS",
           graphics_enhancer_get_filter_name(),
           graphics_enhancer_get_aspect_name());
}

// ---------------------------------------------------------------- menú

enum { ROW_LANG = ACT_COUNT, ROW_FPS, ROW_FILTER, ROW_ASPECT, ROW_DEFAULTS, ROW_COUNT };
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
    else if (cursor == ROW_FPS && (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)))
        controls_set_fps_mode(!g_fps_original);
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

static void text(int x, int y, uint32_t c, const char *s) { ui_text_rgba(menu_px, MENU_W, MENU_H, x, y, 20, c, s); }

static void render(void) {
    fill(0, MENU_H, C_BG);
    text(40, 20, C_HEAD, "PS VITA SETTINGS & CONTROLS");
    for (int r = 0; r < ROW_COUNT; r++) {
        int y = 56 + r * 30;
        if (r == cursor)
            fill(y - 3, 28, C_SEL);
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
            snprintf(val, sizeof(val), "< %s >", g_fps_original ? "60 FPS (ORIGINAL)" : "30 FPS (Battery Saver)");
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
    text(40, 498, C_DIM, "Hotkeys: Select+R: FPS | L+R+TRIANGLE: Filter | L+R+SQUARE: Aspect Ratio");
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
