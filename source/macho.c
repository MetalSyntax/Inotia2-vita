// Loader de Mach-O 32 bits (MH_EXECUTE de iOS 3, ARMv6, dyld clásico).
//
// 1. __TEXT se arma en un buffer, se le aplican las reubicaciones y se copia a un bloque RX;
//    __DATA va directo a un bloque RW justo después (el código es PIC: TEXT y DATA tienen que
//    conservar su distancia).
// 2. rebase.bin (tools/macho_prep.py) lista las palabras con punteros absolutos: se les suma
//    MACHO_SLIDE. Los slots locales de __nl_symbol_ptr también salen de ahí.
// 3. Imports: tabla de símbolos indirectos (__la/__nl_symbol_ptr) + relocs externas. Lo que no
//    resuelve imports.c va a un thunk que registra el nombre y devuelve 0.
#include <kubridge.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imports.h"
#include "log.h"
#include "macho.h"

#define LC_SEGMENT 0x1
#define LC_SYMTAB 0x2
#define LC_DYSYMTAB 0xB
#define LC_ENCRYPTION_INFO 0x21
#define S_ZEROFILL 0x1
#define S_NON_LAZY_SYMBOL_POINTERS 0x6
#define S_LAZY_SYMBOL_POINTERS 0x7
#define INDIRECT_SYMBOL_LOCAL 0x80000000u
#define INDIRECT_SYMBOL_ABS 0x40000000u
#define N_STAB 0xE0
#define N_TYPE 0x0E
#define N_SECT 0x0E
#define N_ARM_THUMB_DEF 0x0008

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX 0x0C20D050
#endif

#define ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define THUNK_ARENA_SIZE 0x20000

typedef struct {
    char segname[16];
    uint32_t vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, flags;
} segment_command;

typedef struct {
    char sectname[16], segname[16];
    uint32_t addr, size, offset, align, reloff, nreloc, flags, reserved1, reserved2;
} section;

typedef struct {
    uint32_t n_strx;
    uint8_t n_type, n_sect;
    uint16_t n_desc;
    uint32_t n_value;
} nlist;

typedef struct {
    uint32_t ilocalsym, nlocalsym, iextdefsym, nextdefsym, iundefsym, nundefsym, tocoff, ntoc,
        modtaboff, nmodtab, extrefsymoff, nextrefsyms, indirectsymoff, nindirectsyms, extreloff,
        nextrel, locreloff, nlocrel;
} dysymtab_command;

static uint8_t *file;  // el Mach-O completo (se conserva: tabla de símbolos para macho_sym)
static nlist *symtab;
static uint32_t nsyms;
static const char *strtab;
static uint32_t text_vm, text_size, data_vm, data_size;
static uint8_t *text_buf;   // TEXT editable antes de copiarse al bloque RX
static uintptr_t text_real; // dirección real de TEXT
static uint32_t mod_init_addr, mod_init_count;

static uint32_t *thunk_head, *thunk_end;
static const char **unimpl_names;
static int unimpl_count;

// El thunk carga su índice en r12 y salta a ml_unimpl_entry (asm.S).
extern void ml_unimpl_entry(void);

void ml_unimpl_log(int idx) {
    static uint8_t seen[4096];
    if (idx < (int)sizeof(seen) && seen[idx] < 3) {
        seen[idx]++;
        l_error("unimpl: %s%s", unimpl_names[idx], seen[idx] == 3 ? " (no se vuelve a mostrar)" : "");
    }
}

static uintptr_t make_unimpl_thunk(const char *name) {
    if (thunk_head + 4 > thunk_end)
        fatal_error("arena de thunks llena");
    unimpl_names = realloc(unimpl_names, (unimpl_count + 1) * sizeof(char *));
    unimpl_names[unimpl_count] = name;
    uint32_t code[4] = {
        0xE59FC004, // ldr ip, [pc, #4]
        0xE59FF000, // ldr pc, [pc, #0]
        (uint32_t)unimpl_count++,
        (uint32_t)&ml_unimpl_entry,
    };
    uintptr_t addr = (uintptr_t)thunk_head;
    kuKernelCpuUnrestrictedMemcpy(thunk_head, code, sizeof(code));
    kuKernelFlushCaches(thunk_head, sizeof(code));
    thunk_head += 4;
    return addr;
}

// Imports de datos sin implementar: bloque en cero (leer da 0/NULL en vez de instrucciones).
static uintptr_t make_unimpl_data(const char *name) {
    l_error("unimpl (dato): %s", name);
    return (uintptr_t)calloc(1, 64);
}

static uint32_t *word_ptr(uint32_t vmaddr) {
    if (vmaddr >= text_vm && vmaddr + 4 <= text_vm + text_size)
        return (uint32_t *)(text_buf + (vmaddr - text_vm));
    if (vmaddr >= data_vm && vmaddr + 4 <= data_vm + data_size)
        return (uint32_t *)(data_vm + MACHO_SLIDE + (vmaddr - data_vm));
    return NULL;
}

static uintptr_t resolve(const char *name, int is_lazy) {
    uintptr_t addr = imports_lookup(name);
    if (addr)
        return addr;
    return is_lazy ? make_unimpl_thunk(name) : make_unimpl_data(name);
}

static int alloc_block(const char *name, int type, uintptr_t addr, uint32_t size) {
    SceKernelAllocMemBlockKernelOpt opt;
    memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt);
    opt.attr = 0x1;
    opt.field_C = addr;
    SceUID id = kuKernelAllocMemBlock(name, type, size, &opt);
    if (id < 0)
        return id;
    void *base;
    sceKernelGetMemBlockBase(id, &base);
    if ((uintptr_t)base != addr)
        fatal_error("%s quedó en %p y no en 0x%08X", name, base, (unsigned)addr);
    return 0;
}

int macho_load(const char *bin_path, const char *rebase_path) {
    FILE *f = fopen(bin_path, "rb");
    if (!f)
        fatal_error("No se encuentra %s (copiá el ejecutable de Inotia2.app)", bin_path);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc(fsize);
    fread(file, 1, fsize, f);
    fclose(f);

    uint32_t *hdr = (uint32_t *)file;
    if (hdr[0] != 0xFEEDFACE || hdr[1] != 12 || hdr[3] != 2)
        fatal_error("%s no es un ejecutable ARM de 32 bits (¿binario universal?)", bin_path);

    segment_command *text_seg = NULL, *data_seg = NULL;
    dysymtab_command *dysym = NULL;
    section *sects[64];
    int nsect = 0;
    uint8_t *lc = file + 28;
    for (uint32_t i = 0; i < hdr[4]; i++) {
        uint32_t cmd = ((uint32_t *)lc)[0], size = ((uint32_t *)lc)[1];
        if (cmd == LC_SEGMENT) {
            segment_command *seg = (segment_command *)(lc + 8);
            if (!strncmp(seg->segname, "__TEXT", 16))
                text_seg = seg;
            else if (!strncmp(seg->segname, "__DATA", 16))
                data_seg = seg;
            section *s = (section *)(seg + 1);
            for (uint32_t j = 0; j < seg->nsects && nsect < 64; j++)
                sects[nsect++] = &s[j];
        } else if (cmd == LC_SYMTAB) {
            uint32_t *st = (uint32_t *)(lc + 8);
            symtab = (nlist *)(file + st[0]);
            nsyms = st[1];
            strtab = (const char *)(file + st[2]);
        } else if (cmd == LC_DYSYMTAB) {
            dysym = (dysymtab_command *)(lc + 8);
        } else if (cmd == LC_ENCRYPTION_INFO && ((uint32_t *)lc)[4] != 0) {
            fatal_error("El ejecutable sigue cifrado (FairPlay cryptid=1): hace falta uno descifrado");
        }
        lc += size;
    }
    if (!text_seg || !data_seg || !symtab || !dysym)
        fatal_error("Mach-O incompleto: faltan __TEXT/__DATA/LC_SYMTAB/LC_DYSYMTAB");

    text_vm = text_seg->vmaddr;
    text_size = text_seg->vmsize;
    data_vm = data_seg->vmaddr;
    data_size = data_seg->vmsize;
    if (data_vm != text_vm + text_size)
        fatal_error("__DATA no está pegado a __TEXT");
    text_real = text_vm + MACHO_SLIDE;
    l_info("Mach-O: TEXT 0x%08X+0x%X DATA 0x%08X+0x%X -> real 0x%08X", (unsigned)text_vm,
           (unsigned)text_size, (unsigned)data_vm, (unsigned)data_size, (unsigned)text_real);

    if (alloc_block("ml_thunks", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, text_real - THUNK_ARENA_SIZE,
                    THUNK_ARENA_SIZE) < 0 ||
        alloc_block("ml_text", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, text_real, ALIGN(text_size, 0x1000)) < 0 ||
        alloc_block("ml_data", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, data_vm + MACHO_SLIDE, ALIGN(data_size, 0x1000)) < 0)
        fatal_error("kuKernelAllocMemBlock falló: ¿está kubridge.skprx en ur0:tai/config.txt?");
    thunk_head = (uint32_t *)(text_real - THUNK_ARENA_SIZE);
    thunk_end = (uint32_t *)text_real;

    text_buf = calloc(1, text_size);
    memcpy(text_buf, file + text_seg->fileoff, text_seg->filesize);
    uint8_t *data_real = (uint8_t *)(data_vm + MACHO_SLIDE);
    memset(data_real, 0, ALIGN(data_size, 0x1000));
    memcpy(data_real, file + data_seg->fileoff, data_seg->filesize);

    // Reubicaciones precalculadas.
    f = fopen(rebase_path, "rb");
    if (!f)
        fatal_error("No se encuentra %s (generalo con tools/macho_prep.py)", rebase_path);
    uint32_t rh[2];
    fread(rh, 4, 2, f);
    if (rh[0] != 0x31534252) // 'RBS1'
        fatal_error("%s no es un rebase.bin válido", rebase_path);
    uint32_t *rebase = malloc(rh[1] * 4);
    fread(rebase, 4, rh[1], f);
    fclose(f);
    for (uint32_t i = 0; i < rh[1]; i++) {
        uint32_t *w = word_ptr(rebase[i]);
        if (!w)
            fatal_error("rebase fuera de la imagen: 0x%08X", (unsigned)rebase[i]);
        *w += MACHO_SLIDE;
    }
    free(rebase);
    l_info("rebase: %u palabras", (unsigned)rh[1]);

    // Imports por la tabla de símbolos indirectos.
    uint32_t *indirect = (uint32_t *)(file + dysym->indirectsymoff);
    int n_imports = 0;
    for (int i = 0; i < nsect; i++) {
        section *s = sects[i];
        uint32_t type = s->flags & 0xFF;
        if (!strncmp(s->sectname, "__mod_init_func", 16)) {
            mod_init_addr = s->addr;
            mod_init_count = s->size / 4;
        }
        if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS)
            continue;
        for (uint32_t j = 0; j < s->size / 4; j++) {
            uint32_t idx = indirect[s->reserved1 + j];
            if (idx & (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS))
                continue; // los locales ya los cubrió rebase.bin
            *word_ptr(s->addr + j * 4) =
                resolve(strtab + symtab[idx].n_strx, type == S_LAZY_SYMBOL_POINTERS);
            n_imports++;
        }
    }
    // Relocs externas (GENERIC_RELOC_VANILLA): slot = símbolo + addend ya escrito en el slot.
    uint32_t *rel = (uint32_t *)(file + dysym->extreloff);
    for (uint32_t i = 0; i < dysym->nextrel; i++) {
        uint32_t addr = rel[i * 2], info = rel[i * 2 + 1];
        uint32_t *w = word_ptr(addr);
        if (!w || ((info >> 28) & 0xF) != 0 || ((info >> 25) & 3) != 2)
            fatal_error("reloc externa no soportada en 0x%08X (info 0x%08X)", (unsigned)addr, (unsigned)info);
        *w += resolve(strtab + symtab[info & 0xFFFFFF].n_strx, 0);
        n_imports++;
    }
    l_info("imports: %d slots, %d sin implementar", n_imports, unimpl_count);

    kuKernelCpuUnrestrictedMemcpy((void *)text_real, text_buf, text_size);
    kuKernelFlushCaches((void *)text_real, text_size);
    free(text_buf);
    text_buf = NULL;
    return 0;
}

void macho_run_inits(void) {
    for (uint32_t i = 0; i < mod_init_count; i++) {
        uintptr_t fn = ((uint32_t *)(mod_init_addr + MACHO_SLIDE))[i];
        l_debug("mod_init[%u] = 0x%08X", (unsigned)i, (unsigned)(fn - MACHO_SLIDE));
        call_guest((void *)fn, 0, 0, 0, 0);
    }
    l_info("inicializadores: %u", (unsigned)mod_init_count);
}

static nlist *find_sym(const char *name) {
    for (uint32_t i = 0; i < nsyms; i++) {
        nlist *s = &symtab[i];
        if (!(s->n_type & N_STAB) && (s->n_type & N_TYPE) == N_SECT && !strcmp(strtab + s->n_strx, name))
            return s;
    }
    return NULL;
}

uintptr_t macho_sym(const char *name) {
    nlist *s = find_sym(name);
    if (!s)
        return 0;
    return (s->n_value + MACHO_SLIDE) | ((s->n_desc & N_ARM_THUMB_DEF) ? 1 : 0);
}

void macho_hook(const char *name, void *dst) {
    nlist *s = find_sym(name);
    if (!s)
        fatal_error("hook: no existe %s", name);
    // Tamaño = distancia al siguiente símbolo de código.
    uint32_t next = 0xFFFFFFFF;
    for (uint32_t i = 0; i < nsyms; i++) {
        nlist *o = &symtab[i];
        if (!(o->n_type & N_STAB) && (o->n_type & N_TYPE) == N_SECT && o->n_sect == s->n_sect &&
            o->n_value > s->n_value && o->n_value < next)
            next = o->n_value;
    }
    uintptr_t addr = s->n_value + MACHO_SLIDE;
    uint32_t patch[2];
    uint32_t need;
    if (s->n_desc & N_ARM_THUMB_DEF) {
        uint16_t nop = 0xBF00;
        need = 8 + (addr & 2);
        if (next - s->n_value < need)
            fatal_error("hook: %s mide %u bytes, no entra el salto", name, (unsigned)(next - s->n_value));
        if (addr & 2) {
            kuKernelCpuUnrestrictedMemcpy((void *)addr, &nop, 2);
            addr += 2;
        }
        patch[0] = 0xF000F8DF; // ldr.w pc, [pc]
    } else {
        need = 8;
        if (next - s->n_value < need)
            fatal_error("hook: %s mide %u bytes, no entra el salto", name, (unsigned)(next - s->n_value));
        patch[0] = 0xE51FF004; // ldr pc, [pc, #-4]
    }
    patch[1] = (uint32_t)dst;
    kuKernelCpuUnrestrictedMemcpy((void *)addr, patch, sizeof(patch));
    kuKernelFlushCaches((void *)(addr & ~3), 16);
}
