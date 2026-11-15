#pragma once
#include <stdint.h>

void gles_init(void);
// Presenta el frame: FBO vertical de iOS -> pantalla apaisada de la Vita.
void gles_present(void);
uint32_t gles_get_draw_calls(void);

// 0 = 20 FPS (ORIGINAL), 1 = 30 FPS (Forced), 2 = 60 FPS (Forced)
void gles_set_fps_mode(int mode);
int gles_get_fps_mode(void);
