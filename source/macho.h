#pragma once
#include <stdint.h>

// El ejecutable iOS está enlazado en 0x1000 y no es PIE. Se carga desplazado: la dirección real
// es la del Mach-O + MACHO_SLIDE. Así un PC de un crash se traduce restando 0x98000000.
#define MACHO_SLIDE 0x98000000u

int macho_load(const char *bin_path, const char *rebase_path);
void macho_run_inits(void);

// Dirección real de un símbolo definido del juego (nombre Mach-O, con '_'); incluye el bit
// Thumb. 0 si no existe.
uintptr_t macho_sym(const char *name);

// Redirige una función del juego a una implementación propia. Falla (fatal) si la función es
// más chica que el parche.
void macho_hook(const char *name, void *dst);

// Llama a una función del juego preservando r9 (el código iOS lo usa como temporal). Usar
// siempre que el llamador NO sea código compilado con -ffixed-r9 (callbacks desde newlib, etc.).
uint32_t call_guest(void *fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
