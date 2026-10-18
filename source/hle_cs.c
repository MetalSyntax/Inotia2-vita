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
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cgbi.h"
#include "gles.h"
#include "hle.h"
#include "imports.h"
#include "log.h"
#include "macho.h"

// Códigos de error WIPI que usa la versión iOS.
#define M_E_BADFD (-2)
#define M_E_INVALID (-9)
#define M_E_NOENT (-12)
#define M_E_BADATTR (-24)

// ---------------------------------------------------------------- archivos (saves/)

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
    case 2: m = "r+b"; break; // fileHandleForWritingAtPath: desde 0, sin crear ni truncar
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

// whence 2 = fin - off: así lo calculan CS_fsSeek y MC_fsSeek de iOS.
static int seek_file(FILE *f, int off, int whence) {
    int pos;
    if (whence == 0) {
        pos = off;
    } else if (whence == 1) {
        pos = ftell(f) + off;
    } else if (whence == 2) {
        fseek(f, 0, SEEK_END);
        pos = ftell(f) - off;
    } else {
        return M_E_INVALID;
    }
    if (pos != -1)
        fseek(f, pos, SEEK_SET);
    return pos;
}

static int cs_fsSeek(int h, int off, int whence) {
    FILE *f = file_of(h);
    return f ? seek_file(f, off, whence) : M_E_BADFD;
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

// ---------------------------------------------------------------- MC_fs* (saves/)

// Segunda API de archivos (partidas/NV), en iOS sobre NSFileHandle: descriptores 0..14, -1 si
// falla, y crea el archivo si no existe en cualquier modo (4 = escritura truncando).
#define MC_FILES 15
static FILE *mc_files[MC_FILES];

static FILE *mc_of(int fd) { return (fd >= 0 && fd < MC_FILES) ? mc_files[fd] : NULL; }

static int mc_fsOpen(const char *name, int mode, int amode) {
    char p[512];
    doc_path(name, p, sizeof(p));
    FILE *f = fopen(p, mode == 4 ? "w+b" : "r+b");
    if (!f)
        f = fopen(p, "w+b");
    l_debug("MC_fsOpen(%s, %d) -> %p", p, mode, f);
    for (int i = 0; f && i < MC_FILES; i++)
        if (!mc_files[i]) {
            mc_files[i] = f;
            return i;
        }
    if (f)
        fclose(f);
    return -1;
}

static int mc_fsClose(int fd) {
    FILE *f = mc_of(fd);
    if (f) {
        fclose(f);
        mc_files[fd] = NULL;
    }
    return 0;
}

static int mc_fsRead(int fd, void *buf, int len) {
    FILE *f = mc_of(fd);
    return f ? (int)fread(buf, 1, len, f) : M_E_NOENT;
}

static int mc_fsWrite(int fd, const void *buf, int len) {
    FILE *f = mc_of(fd);
    if (!f)
        return M_E_NOENT;
    fwrite(buf, 1, len, f);
    fflush(f);
    return len;
}

static int mc_fsSeek(int fd, int off, int whence) {
    FILE *f = mc_of(fd);
    return f ? seek_file(f, off, whence) : M_E_BADFD;
}

// Ojo: 0 = existe, 1 = no existe.
static int mc_fsIsExist(const char *name, int amode) {
    char p[512];
    SceIoStat st;
    return sceIoGetstat(doc_path(name, p, sizeof(p)), &st) < 0;
}

static int mc_fsRemove(const char *name) {
    char p[512];
    sceIoRemove(doc_path(name, p, sizeof(p)));
    return 0;
}

static int mc_fsAvailable(void) { return 256 * 1024 * 1024; }

// ---------------------------------------------------------------- imágenes

static void *knl_calloc(size_t n) { return (void *)call_guest((void *)macho_sym("_CS_knlCalloc"), n, 0, 0, 0); }

// PNG estándar -> RGBA8 premultiplicado (libpng).
static uint8_t *decode_png(const void *data, int len, int *w, int *h, int *bpp) {
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, data, len)) {
        l_error("PNG inválido (%d bytes): %s", len, img.message);
        return NULL;
    }
    *bpp = (img.format & PNG_FORMAT_FLAG_COLORMAP) ? 8 : PNG_IMAGE_PIXEL_SIZE(img.format) * 8;
    *w = img.width;
    *h = img.height;
    uint8_t *px = knl_calloc(*w * *h * 4);
    img.format = PNG_FORMAT_RGBA;
    if (!px || !png_image_finish_read(&img, NULL, px, 0, NULL)) {
        l_error("PNG: no se pudo decodificar (%dx%d): %s", *w, *h, img.message);
        return NULL;
    }
    for (uint8_t *p = px; p < px + *w * *h * 4; p += 4)
        if (p[3] != 255) {
            p[0] = p[0] * p[3] / 255;
            p[1] = p[1] * p[3] / 255;
            p[2] = p[2] * p[3] / 255;
        }
    return px;
}

// Reemplaza a UIImage/CGImage: imagen -> RGBA8 premultiplicado (kCGImageAlphaPremultipliedLast),
// fila superior primero, en memoria de CS_knlCalloc. info = {w, h, bytesPerRow, bitsPerPixel}
// del original. Los PNG del bundle son CgBI de Apple y se leen tal cual (cgbi.c); un PNG estándar
// va por libpng. Después, la misma pasada que el juego: flag 0 -> magenta (255,0,255) con A=0;
// flag != 0 -> A = R|G|B.
static int util_CreateTextureData(int *info, const void *data, int len, int flag) {
    int w, h, bpp;
    uint8_t *px = cgbi_is(data, len) ? cgbi_decode(data, len, &w, &h, &bpp, knl_calloc) : decode_png(data, len, &w, &h, &bpp);
    if (!px) {
        l_error("imagen no decodificada (%d bytes)", len);
        return -6;
    }
    for (uint8_t *p = px; p < px + w * h * 4; p += 4) {
        if (flag)
            p[3] = p[0] | p[1] | p[2];
        else if (p[0] == 255 && p[1] == 0 && p[2] == 255)
            p[3] = 0;
    }
    info[0] = w;
    info[1] = h;
    info[2] = (w * bpp + 7) / 8;
    info[3] = bpp;
    return (int)px;
}

// ---------------------------------------------------------------- recursos del bundle

// Resource ID = índice en una tabla de rutas (>= 0; los llamadores como LoadFile toman < 0 como
// error). CS_knlGetResource lo consume, como el CFURL original.
#define MAX_RES 64
static char *res_paths[MAX_RES];

static int cs_knlGetResourceID(const char *name, int *size) {
    static const char *dirs[] = {BUNDLE_PATH "English.lproj/", BUNDLE_PATH};
    *size = 0;
    for (int i = 0; i < 2; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s%s", dirs[i], name);
        SceIoStat st;
        if (sceIoGetstat(p, &st) < 0)
            continue;
        for (int id = 0; id < MAX_RES; id++)
            if (!res_paths[id]) {
                res_paths[id] = strdup(p);
                *size = (int)st.st_size;
                l_debug("CS_knlGetResourceID(%s) -> id %d, size %d", name, id, *size);
                return id;
            }
        l_error("CS_knlGetResourceID: más de %d recursos abiertos", MAX_RES);
        return M_E_INVALID;
    }
    l_error("recurso no encontrado: %s", name);
    return M_E_NOENT;
}

static int cs_knlGetResource(int id, void *buf, int size) {
    if (id < 0 || id >= MAX_RES || !res_paths[id])
        return M_E_INVALID;
    FILE *f = fopen(res_paths[id], "rb");
    int n = f ? (int)fread(buf, 1, size, f) : 0;
    l_debug("CS_knlGetResource(%d (%s), buf=%p, size=%d) -> %d bytes", id, res_paths[id], buf, size, n);
    if (f)
        fclose(f);
    free(res_paths[id]);
    res_paths[id] = NULL;
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

// ---------------------------------------------------------------- pantalla

static int frames;
static void cs_grpFlushLcd(void) {
    if (++frames % 600 == 1)
        l_info("CS_grpFlushLcd: frame %d (draws=%u)", frames, gles_get_draw_calls());
    // El original también actualiza FMOD acá (FMOD_EventSystem_Update + [glView flushLCD]).
    uint32_t sys = *(uint32_t *)macho_sym("_snd_eventSystem");
    if (sys)
        call_guest((void *)macho_sym("_FMOD_EventSystem_Update"), sys, 0, 0, 0);
    gles_present();
}

// ---------------------------------------------------------------- audio

// CSX_fmodInitialize pide FMOD_INIT_ENABLE_PROFILE (0x20): el profiler abre un socket, que acá
// falla, y EventSystem::init devuelve error -> nunca se llega a cargar Inotia.fev (sin sonido).
static int fmod_EventSystem_Init(void **sys, int maxch, unsigned flags, void *extra, unsigned evflags) {
    if (!sys)
        return 37;
    int r = call_guest6((*(void ***)sys)[0], (uint32_t)sys, maxch, flags & ~0x20u, (uint32_t)extra, evflags, 0);
    l_info("FMOD_EventSystem_Init(%d, 0x%X) = %d", maxch, flags, r);
    return r;
}

// Los mezcladores *_SIMD y el resampler lineal de FMOD usan el modo vectorial de VFPv2 (FPSCR.LEN),
// que el Cortex-A9 no tiene: instrucción indefinida en el hilo de mezcla. Versiones escalares.
SOFTFP static void fmod_MixMonoToStereo(const float *in, float *out, unsigned n, float l, float r) {
    for (unsigned i = 0; i < n; i++) {
        out[2 * i] += in[i] * l;
        out[2 * i + 1] += in[i] * r;
    }
}

SOFTFP static void fmod_MixStereoToStereo(const float *in, float *out, unsigned n, float l, float r) {
    for (unsigned i = 0; i < n; i++) {
        out[2 * i] += in[2 * i] * l;
        out[2 * i + 1] += in[2 * i + 1] * r;
    }
}

// La ruta del .fev sale de [[NSBundle mainBundle] resourcePath] (objc.c). Los .fsb están al lado:
// se fija el media path para que FMOD los busque ahí y no en el directorio actual.
static int fmod_EventSystem_Load(void **sys, const char *name, void *info, void *project) {
    if (!sys)
        return 37; // FMOD_ERR_INVALID_HANDLE
    if (!name || !*name)
        name = BUNDLE_PATH "Inotia.fev";
    // EventSystemI::load abre mediaPath + name (strcpy+strcat): con la ruta completa queda duplicada
    // ("…/Inotia2.app/ux0:…/Inotia.fev") y da FMOD_ERR_FILE_NOTFOUND (23). Pasar solo el nombre.
    if (strrchr(name, '/'))
        name = strrchr(name, '/') + 1;
    call_guest((void *)macho_sym("__ZN4FMOD12EventSystemI12setMediaPathEPKc"), (uint32_t)sys, (uint32_t)BUNDLE_PATH, 0, 0);
    int r = call_guest((*(void ***)sys)[9], (uint32_t)sys, (uint32_t)name, (uint32_t)info, (uint32_t)project);
    l_info("FMOD_EventSystem_Load(%s) = %d", name, r);
    return r;
}

// ---------------------------------------------------------------- eventos que en iOS vuelven por ObjC

// Video, "más juegos", selector de música: el juego duerme el dibujo (g_SleepFlush) y espera que
// UIKit le avise con EX_handleEvent(n). Se avisa en el próximo tick, no dentro del hook: el
// llamador todavía hace cosas al volver (p. ej. pausar el sonido después de playMoviePlayer).
static int pending[8], npending;

static void defer_event(int ev) {
    if (npending < 8)
        pending[npending++] = ev;
}

// ponytail: los videos (prologue/epilogue.mp4) se saltean; reproducirlos es SceAvPlayer acá.
static void play_movie(int which) {
    l_info("video %d salteado", which);
    defer_event(6); // = -[MyMoviePlayer myMovieFinishedCallback:]
}
static void start_more_games(void) { defer_event(4); }
static void add_music_or_show_music(void) { defer_event(1); }

void hle_frame(void) {
    ime_poll();
    for (int i = 0; i < npending; i++)
        call_guest((void *)macho_sym("_EX_handleEvent"), pending[i], 0, 0, 0);
    npending = 0;
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
static int ret_m1(void) { return -1; }
static int ret0(void) { return 0; }

// MemCpyMedia / MemCpyMedia2: copias en el heap de medios (_WV_media_buffer).
// Si algún llamador pasa por error un puntero absoluto de __DATA (>= MACHO_SLIDE) en vez del offset
// de medio (como el error original de _LoadString con &_SAVE_FILE_NAME), lo desreferenciamos de forma segura.
static void hook_MemCpyMedia2(uint32_t dst, uint32_t dst_sub, const void *src, uint32_t src_sub, uint32_t size) {
    uint8_t **p_media_buf = (uint8_t **)macho_sym("_WV_media_buffer");
    uint8_t *media_buf = p_media_buf ? *p_media_buf : NULL;
    if (!media_buf) {
        l_error("MemCpyMedia2: _WV_media_buffer es NULL");
        return;
    }
    if (dst >= MACHO_SLIDE) {
        l_warn("MemCpyMedia2: dst (0x%08X) era puntero, resolviendo a offset 0x%08X", (unsigned)dst, (unsigned)*(uint32_t *)dst);
        dst = *(uint32_t *)dst;
    }
    memcpy(media_buf + dst + dst_sub, (const uint8_t *)src + src_sub, size);
}

static void hook_MemCpyMedia(uint32_t dst, uint32_t dst_sub, uint32_t src, uint32_t src_sub, uint32_t size) {
    uint8_t **p_media_buf = (uint8_t **)macho_sym("_WV_media_buffer");
    uint8_t *media_buf = p_media_buf ? *p_media_buf : NULL;
    if (!media_buf) {
        l_error("MemCpyMedia: _WV_media_buffer es NULL");
        return;
    }
    if (dst >= MACHO_SLIDE) {
        l_warn("MemCpyMedia: dst (0x%08X) era puntero, resolviendo a offset 0x%08X", (unsigned)dst, (unsigned)*(uint32_t *)dst);
        dst = *(uint32_t *)dst;
    }
    if (src >= MACHO_SLIDE) {
        l_warn("MemCpyMedia: src (0x%08X) era puntero, resolviendo a offset 0x%08X", (unsigned)src, (unsigned)*(uint32_t *)src);
        src = *(uint32_t *)src;
    }
    memcpy(media_buf + dst + dst_sub, media_buf + src + src_sub, size);
}

void hle_cs_install(void) {
    sceIoMkdir(DATA_PATH "saves", 0777);
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

    macho_hook("_MC_fsOpen", mc_fsOpen);
    macho_hook("_MC_fsClose", mc_fsClose);
    macho_hook("_MC_fsRead", mc_fsRead);
    macho_hook("_MC_fsWrite", mc_fsWrite);
    macho_hook("_MC_fsSeek", mc_fsSeek);
    macho_hook("_MC_fsIsExist", mc_fsIsExist);
    macho_hook("_MC_fsRemove", mc_fsRemove);
    macho_hook("_MC_fsAvailable", mc_fsAvailable);
    macho_hook("___utilCreateTextureDataFromIphoneSDK", util_CreateTextureData);
    macho_hook("_FMOD_EventSystem_Init", fmod_EventSystem_Init);
    macho_hook("_FMOD_EventSystem_Load", fmod_EventSystem_Load);
    macho_hook("_FMOD_DSP_Connection_MixMonoToStereo_SIMD", fmod_MixMonoToStereo);
    macho_hook("_FMOD_DSP_Connection_MixStereoToStereo_SIMD", fmod_MixStereoToStereo);
    // ponytail: mismos argumentos que Linear pero sin interpolar (algo de aliasing); portar Linear a C si se nota.
    macho_hook("_FMOD_Resampler_Linear", (void *)macho_sym("_FMOD_Resampler_NoInterp"));
    macho_hook("_playMoviePlayer", play_movie);
    macho_hook("_endMoviePlayer", noop);
    macho_hook("_StartMoreGames", start_more_games);
    macho_hook("_AddMusicOrShowMusic", add_music_or_show_music);
    // Anti-piratería: la clave la carga -[EAGLView initWithCoder:], que no corre; sin esto el
    // primer toque en el menú lee fuera de la tabla.
    macho_hook("_VerifyApplication1", noop);
    macho_hook("_Cracked_GotoHomepage", noop);
    // Navegador/tienda/red: offline. Store_Start dejaría al juego esperando a StoreKit.
    macho_hook("_BackToBrowser_iPhone", noop);
    macho_hook("_GotoTipsBoard", noop);
    macho_hook("_Store_Start", noop);
    macho_hook("_Store_Buy", noop);
    macho_hook("_Store_End", noop);
    macho_hook("_Store_AppFinalize", noop);
    macho_hook("___startObserver", noop);
    macho_hook("_CS_netSocketConnect", ret_m1);
    macho_hook("_CS_netSocketConnectEx", ret_m1);
    macho_hook("_CS_netGetActiveWiFi", ret0);
    macho_hook("_MemCpyMedia2", hook_MemCpyMedia2);
    macho_hook("_MemCpyMedia", hook_MemCpyMedia);
}
