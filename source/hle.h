#pragma once
#include <stddef.h>
#include <stdint.h>

// ux0:data/inotia2/ imita el sandbox de iOS (ver tools/stage_data.sh).
#define BUNDLE_PATH DATA_PATH "Inotia2.app/"
#define DOCS_PATH DATA_PATH "saves/" // Documents/ de iOS
#define LOGS_PATH DATA_PATH "logs/"

void darwin_init(void);
const char *darwin_path(const char *in, char *out, size_t outlen);

void hle_cs_install(void);
uint64_t hle_run_timers(void);

// Un tick del bucle principal: eventos diferidos al motor e IME.
void hle_frame(void);

// objc.c: diálogo de texto (sceImeDialog) abierto por textInputOpen: del juego.
int ime_active(void);
void ime_poll(void);

// input.c: touch + botones -> eventos del motor.
void input_init(void);
void input_poll(void);
