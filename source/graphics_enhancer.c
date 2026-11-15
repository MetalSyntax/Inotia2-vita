#include "graphics_enhancer.h"
#include "log.h"

#define SCREEN_W 960.0f
#define SCREEN_H 544.0f
#define GAME_W 480.0f
#define GAME_H 320.0f

static GraphicsFilterMode s_current_filter = FILTER_SHARP_PIXEL_ART;
static GraphicsAspectMode s_current_aspect = ASPECT_RATIO_FULLSCREEN;

static const char *s_filter_names[FILTER_MAX_COUNT] = {
    "Sharp Pixel-Art 2x (Crisp)",
    "Smooth Bilinear 2x",
    "Vibrant OLED/LCD (Crisp)",
    "Vibrant OLED/LCD (Smooth)"
};

static const char *s_aspect_names[ASPECT_MAX_COUNT] = {
    "Aspect Ratio 3:2 Fit (816x544)",
    "Fullscreen Stretch (960x544)",
    "Integer 1x Center (480x320)"
};

void graphics_enhancer_init(void) {
    l_info("[Graphics] Enhancer initialized. Filter: %s | Aspect: %s",
           s_filter_names[s_current_filter], s_aspect_names[s_current_aspect]);
}

void graphics_enhancer_get_render_bounds(float *out_x, float *out_y, float *out_w, float *out_h) {
    float rx, ry, rw, rh;

    switch (s_current_aspect) {
        case ASPECT_RATIO_INTEGER_1X: // 480 x 320 centered
            rw = 480.0f;
            rh = 320.0f;
            rx = (SCREEN_W - rw) * 0.5f; // 240.0f
            ry = (SCREEN_H - rh) * 0.5f; // 112.0f
            break;

        case ASPECT_RATIO_3_2_FIT: // 816 x 544 (3:2 ratio)
            rh = 544.0f;
            rw = 544.0f * (3.0f / 2.0f); // 816.0f
            rx = (SCREEN_W - rw) * 0.5f; // 72.0f
            ry = 0.0f;
            break;

        case ASPECT_RATIO_FULLSCREEN: // 960 x 544
        default:
            rw = 960.0f;
            rh = 544.0f;
            rx = 0.0f;
            ry = 0.0f;
            break;
    }

    if (out_x) *out_x = rx;
    if (out_y) *out_y = ry;
    if (out_w) *out_w = rw;
    if (out_h) *out_h = rh;
}

void graphics_enhancer_cycle_filter(void) {
    s_current_filter = (GraphicsFilterMode)((s_current_filter + 1) % FILTER_MAX_COUNT);
    l_info("[Graphics] Filter changed to: %s", s_filter_names[s_current_filter]);
}

void graphics_enhancer_cycle_aspect(void) {
    s_current_aspect = (GraphicsAspectMode)((s_current_aspect + 1) % ASPECT_MAX_COUNT);
    l_info("[Graphics] Aspect Ratio changed to: %s", s_aspect_names[s_current_aspect]);
}

void graphics_enhancer_set_filter(GraphicsFilterMode filter) {
    if (filter >= 0 && filter < FILTER_MAX_COUNT)
        s_current_filter = filter;
}

void graphics_enhancer_set_aspect(GraphicsAspectMode aspect) {
    if (aspect >= 0 && aspect < ASPECT_MAX_COUNT)
        s_current_aspect = aspect;
}

GraphicsFilterMode graphics_enhancer_get_filter(void) {
    return s_current_filter;
}

GraphicsAspectMode graphics_enhancer_get_aspect(void) {
    return s_current_aspect;
}

const char *graphics_enhancer_get_filter_name(void) {
    return s_filter_names[s_current_filter];
}

const char *graphics_enhancer_get_aspect_name(void) {
    return s_aspect_names[s_current_aspect];
}

GLenum graphics_enhancer_get_gl_filter(void) {
    switch (s_current_filter) {
        case FILTER_SMOOTH_BILINEAR:
        case FILTER_VIBRANT_SMOOTH:
            return GL_LINEAR;
        case FILTER_SHARP_PIXEL_ART:
        case FILTER_VIBRANT_OLED:
        default:
            return GL_NEAREST;
    }
}

float graphics_enhancer_get_color_mult(void) {
    switch (s_current_filter) {
        case FILTER_VIBRANT_OLED:
        case FILTER_VIBRANT_SMOOTH:
            return 1.15f;
        case FILTER_SHARP_PIXEL_ART:
        case FILTER_SMOOTH_BILINEAR:
        default:
            return 1.0f;
    }
}
