#pragma once
#include <stdint.h>

typedef struct {
    const char *name; // nombre Mach-O (con '_' inicial)
    uintptr_t addr;
    int data; // 1 = variable (se enlaza directo); 0 = función (entra por ml_align_call)
} import_entry;

// Devuelve la implementación de un import del juego, o NULL si no hay.
const import_entry *imports_lookup(const char *name);

// Tablas por módulo (cada .c exporta la suya; imports.c las recorre).
extern const import_entry darwin_imports[];
extern const int darwin_imports_count;
extern const import_entry gles_imports[];
extern const int gles_imports_count;
extern const import_entry coreaudio_imports[];
extern const int coreaudio_imports_count;
extern const import_entry objc_imports[];
extern const int objc_imports_count;

// Convención softfp de iOS ARMv6: floats/doubles viajan en registros enteros. La Vita es hardfp,
// así que todo import con float en la firma se implementa con este atributo.
#define SOFTFP __attribute__((pcs("aapcs")))
