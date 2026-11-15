#pragma once
#include <stdint.h>

void gles_init(void);
// Presenta el frame: FBO vertical de iOS -> pantalla apaisada de la Vita.
void gles_present(void);
uint32_t gles_get_draw_calls(void);

// 0 = 30 FPS cap (interval=2), 1 = 60 FPS / ORIGINAL (interval=1)
void gles_set_fps_mode(int original);
int gles_get_fps_mode(void);
