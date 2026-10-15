// Reemplazo de la capa de plataforma WIPI de Com2uS (CS_*) que en iOS usa Foundation/CoreFoundation.
// La semántica sale del desensamblado de Inotia2 (tools/annotate.py) y del CS_fsOpen de Inotia 3
// Android (mismo motor, implementado con open()).
//
// OJO ABI iOS: los argumentos de 64 bits NO se alinean a par de registros (CS_knlSetTimer recibe el
// int64 en r1:r2). Los hooks con int64 se declaran partidos en dos uint32_t.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hle.h"
#include "log.h"
#include "macho.h"

#define DOCS_PATH DATA_PATH "Documents/"

// Códigos de error WIPI que usa la versión iOS.
#define M_E_BADFD (-2)
#define M_E_INVALID (-9)
#define M_E_NOENT (-12)
#define M_E_BADATTR (-24)

// ---------------------------------------------------------------- archivos (Documents/)

#define MAX_FILES 64
static FILE *files[MAX_FILES]; // handle = índice + 1 (0 es "inválido" para el juego)

static const char *doc_path(const char *name, char *out, size_t len) {
    if (name[0] == '/' || !strncmp(name, "ux0:", 4))
        return darwin_path(name, out, len);
    snprintf(out, len, DOCS_PATH "%s", name);
    return out;
}

static FILE *file_of(int h) { return (h > 0 && h <= MAX_FILES) ? files[h - 1] : NULL; }

static int cs_fsOpen(const char *name, int mode, int attr) {
    if (attr != 1)
        return M_E_BADATTR;
    char p[512];
    doc_path(name, p, sizeof(p));
    const char *m;
    switch (mode) {
    case 1: m = "rb"; break;  // lectura
    case 2: m = "ab"; break;  // append (crea)
    case 4: m = "wb"; break;  // escritura truncando (crea)
    case 8: m = "r+b"; break; // lectura/escritura (crea)
    default: return M_E_INVALID;
    }
    FILE *f = fopen(p, m);
    if (!f && mode == 8)
        f = fopen(p, "w+b");
    l_debug("CS_fsOpen(%s, %d) -> %p", p, mode, f);
    if (!f)
        return M_E_NOENT;
    for (int i = 0; i < MAX_FILES; i++)
        if (!files[i]) {
            files[i] = f;
            return i + 1;
        }
    fclose(f);
    l_error("CS_fsOpen: sin handles libres");
    return M_E_INVALID;
}

static int cs_fsClose(int h) {
    FILE *f = file_of(h);
    if (!f)
        return M_E_BADFD;
    fclose(f);
    files[h - 1] = NULL;
    return 0;
}

static int cs_fsRead(int h, void *buf, int len) {
    FILE *f = file_of(h);
    return f ? (int)fread(buf, 1, len, f) : M_E_BADFD;
}

static int cs_fsWrite(int h, const void *buf, int len) {
    FILE *f = file_of(h);
    if (!f)
        return M_E_BADFD;
    fwrite(buf, 1, len, f);
    fflush(f); // el original hace synchronizeFile en cada escritura
    return len;
}

static int cs_fsSeek(int h, int off, int whence) {
    FILE *f = file_of(h);
    if (!f)
        return M_E_BADFD;
    int pos;
    if (whence == 0) {
        pos = off;
    } else if (whence == 1) {
        pos = ftell(f) + off;
    } else if (whence == 2) {
        fseek(f, 0, SEEK_END);
        pos = ftell(f) - off; // así lo calcula la versión iOS (end - off)
    } else {
        return M_E_INVALID;
    }
    if (pos != -1)
        fseek(f, pos, SEEK_SET);
    return pos;
}

static int cs_fsIsExist(const char *name, int attr) {
    if (attr != 1)
        return M_E_BADATTR;
    char p[512];
    SceIoStat st;
    return sceIoGetstat(doc_path(name, p, sizeof(p)), &st) < 0 ? M_E_NOENT : 0;
}

static int cs_fsSize(const char *name, int attr) {
    if (attr != 1)
        return M_E_BADATTR;
    char p[512];
    SceIoStat st;
    return sceIoGetstat(doc_path(name, p, sizeof(p)), &st) < 0 ? M_E_NOENT : (int)st.st_size;
}

static int cs_fsRemove(const char *name, int attr) {
    if (attr != 1)
        return M_E_BADATTR;
    char p[512];
    return sceIoRemove(doc_path(name, p, sizeof(p))) < 0 ? M_E_NOENT : 0;
}

// ---------------------------------------------------------------- recursos del bundle

// Resource "ID" = ruta (malloc). CS_knlGetResource la consume, como el CFURL original.
static char *cs_knlGetResourceID(const char *name, int *size) {
    static const char *dirs[] = {DATA_PATH "English.lproj/", DATA_PATH};
    for (int i = 0; i < 2; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s%s", dirs[i], name);
        SceIoStat st;
        if (sceIoGetstat(p, &st) >= 0) {
            *size = (int)st.st_size;
            return strdup(p);
        }
    }
    l_error("recurso no encontrado: %s", name);
    *size = 0;
    return NULL;
}

static int cs_knlGetResource(char *id, void *buf, int size) {
    if (!id)
        return 0;
    FILE *f = fopen(id, "rb");
    int n = f ? (int)fread(buf, 1, size, f) : 0;
    if (f)
        fclose(f);
    free(id);
    return n;
}

static int cs_knlGetSystemProperty(const char *name, char *buf, int len) {
    if (strcmp(name, "PHONENUMBER"))
        return M_E_INVALID;
    strcpy(buf, "VITA000000000000000000000000000000000000"); // UDID de 40 caracteres
    return 0;
}

// ---------------------------------------------------------------- timers

// MCTimer del juego: {int64 timeout; int param; void *cftimer; void (*cb)(MCTimer*, int)}
typedef struct {
    uint32_t ms_lo, ms_hi;
    int param;
    uint32_t scheduled; // en iOS es el CFRunLoopTimer; acá 1 = registrado
    void *cb;
} mc_timer;

#define MAX_TIMERS 32
static struct {
    mc_timer *t;
    uint64_t due; // 0 = no programado
} timers[MAX_TIMERS];

static void cs_knlSetTimer(mc_timer *t, uint32_t ms_lo, uint32_t ms_hi, int param) {
    int64_t ms = ((int64_t)ms_hi << 32) | ms_lo;
    t->ms_lo = ms_lo;
    t->ms_hi = ms_hi;
    t->param = param;
    t->scheduled = 1;
    int free_slot = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].t == t) {
            timers[i].due = sceKernelGetProcessTimeWide() + ms * 1000 + 1;
            return;
        }
        if (!timers[i].t && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        fatal_error("CS_knlSetTimer: más de %d timers", MAX_TIMERS);
    timers[free_slot].t = t;
    timers[free_slot].due = sceKernelGetProcessTimeWide() + ms * 1000 + 1;
}

static void cs_knlUnsetTimer(mc_timer *t) {
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timers[i].t == t)
            timers[i].t = NULL;
    t->scheduled = 0;
}

// Equivalente del CFRunLoop principal: dispara los timers vencidos. Devuelve µs hasta el próximo.
uint64_t hle_run_timers(void) {
    uint64_t now = sceKernelGetProcessTimeWide(), next = 16000;
    for (int i = 0; i < MAX_TIMERS; i++) {
        mc_timer *t = timers[i].t;
        if (!t || !timers[i].due)
            continue;
        if (timers[i].due <= now) {
            timers[i].due = 0; // se reprograma solo si el callback vuelve a llamar SetTimer
            call_guest(t->cb, (uint32_t)t, (uint32_t)t->param, 0, 0);
        } else if (timers[i].due - now < next) {
            next = timers[i].due - now;
        }
    }
    return next;
}

// ---------------------------------------------------------------- pantalla (provisorio)

static int frames;
static void cs_grpFlushLcd(void) {
    if (++frames % 60 == 1)
        l_info("CS_grpFlushLcd: frame %d", frames);
}

// ---------------------------------------------------------------- sistema

// 0 inglés, 1 coreano, 2 japonés (en iOS sale de AppleLanguages).
static int get_lang_config(void) { return 0; }

// Fecha de instalación: g_InstalledDate = {char local[32]; char gmt[32]} en installed1.sav.
static void installed_system(void) {
    char *date = (char *)macho_sym("_g_InstalledDate");
    int h = cs_fsOpen("installed1.sav", 1, 1);
    if (h > 0) {
        cs_fsRead(h, date, 64);
        cs_fsClose(h);
        return;
    }
    time_t now = time(NULL);
    memset(date, 0, 64);
    strftime(date, 32, "%Y-%m-%d %H:%M:%S", localtime(&now));
    strftime(date + 32, 32, "%Y-%m-%d %H:%M:%S", gmtime(&now));
    h = cs_fsOpen("installed1.sav", 8, 1);
    if (h > 0) {
        cs_fsWrite(h, date, 64);
        cs_fsClose(h);
    }
}

static void noop(void) {}

void hle_cs_install(void) {
    sceIoMkdir(DATA_PATH "Documents", 0777);
    macho_hook("_CS_fsOpen", cs_fsOpen);
    macho_hook("_CS_fSClose", cs_fsClose);
    macho_hook("_CS_fsRead", cs_fsRead);
    macho_hook("_CS_fsWrite", cs_fsWrite);
    macho_hook("_CS_fsSeek", cs_fsSeek);
    macho_hook("_CS_fsIsExist", cs_fsIsExist);
    macho_hook("_CS_fsSize", cs_fsSize);
    macho_hook("_CS_fsRemove", cs_fsRemove);
    macho_hook("_CS_knlGetResourceID", cs_knlGetResourceID);
    macho_hook("_CS_knlGetResource", cs_knlGetResource);
    macho_hook("_CS_knlGetSystemProperty", cs_knlGetSystemProperty);
    macho_hook("_CS_knlSetTimer", cs_knlSetTimer);
    macho_hook("_CS_knlUnsetTimer", cs_knlUnsetTimer);
    macho_hook("_CS_grpFlushLcd", cs_grpFlushLcd);
    macho_hook("_GetLangConfig_iPhone", get_lang_config);
    macho_hook("_Installed_System", installed_system);
    // Analítica online: sin efecto offline.
    macho_hook("_Flurry_Start", noop);
    macho_hook("_Flurry_Event", noop);
}
