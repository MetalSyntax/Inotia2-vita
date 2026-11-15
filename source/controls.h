#pragma once
// Botones reasignables + menú del port (START+SELECT), como el de Zenonia 2 / Destinia.
#include <stdint.h>
#include <vitaGL.h>
#include <psp2/ctrl.h>

// Acciones del juego: cada una se manda como toque sobre un botón del pad virtual (input.c).
enum { ACT_ATTACK, ACT_SKILL1, ACT_SKILL2, ACT_SKILL3, ACT_SKILL4, ACT_ITEM, ACT_MENU, ACT_MAP, ACT_COUNT };

void controls_init(void);              // lee ux0:data/inotia2/controls.txt (o lo crea)
void controls_save(void);              // guarda ux0:data/inotia2/controls.txt
uint32_t controls_binding(int action); // SCE_CTRL_* asignado a la acción

// Carpeta con strindex.dat/strdata.dat traducidos (DATA_PATH "lang/spa/"), NULL = inglés.
const char *controls_lang_path(void);

// FPS mode (0 = 30 FPS cap, 1 = 60 FPS / ORIGINAL)
int controls_get_fps_mode(void);
void controls_set_fps_mode(int original);
void controls_toggle_fps(void);

int controls_menu_active(void);
void controls_menu_open(void);
// Botones recién apretados con el menú abierto; devuelve 1 el frame en que se cierra.
int controls_menu_update(uint32_t pressed);
GLuint controls_menu_texture(void); // 960x544 RGBA, se dibuja encima del juego
