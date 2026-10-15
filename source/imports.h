#pragma once
#include <stdint.h>

typedef struct {
    const char *name; // nombre Mach-O (con '_' inicial)
    uintptr_t addr;
} import_entry;

// Devuelve la implementación de un import del juego, o 0 si no hay.
uintptr_t imports_lookup(const char *name);

// Tablas por módulo (cada .c exporta la suya; imports.c las recorre).
extern const import_entry darwin_imports[];
extern const int darwin_imports_count;

// Convención softfp de iOS ARMv6: floats/doubles viajan en registros enteros. La Vita es hardfp,
// así que todo import con float en la firma se implementa con este atributo.
#define SOFTFP __attribute__((pcs("aapcs")))
