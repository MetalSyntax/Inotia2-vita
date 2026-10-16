#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

static SceUID log_fd = -1;

void log_open(const char *path) {
    log_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
}

// Líneas idénticas seguidas (sin contar el timestamp) se aplanan: se reescribe la última línea con
// " (xN)" en vez de agregar otra. El archivo siempre queda con el contador al día, aunque el
// proceso muera en medio de la racha.
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static char last_body[1024];
static unsigned long long last_ts;
static int repeats;
static SceOff last_off;

static void vlog(const char *tag, const char *fmt, va_list ap) {
    char body[1024], line[1100];
    int n = snprintf(body, sizeof(body), "%s ", tag);
    vsnprintf(body + n, sizeof(body) - n, fmt, ap);
    pthread_mutex_lock(&log_lock);
    if (repeats && !strcmp(body, last_body)) {
        repeats++;
        n = snprintf(line, sizeof(line), "[%llu] %s (x%d)\n", last_ts, body, repeats);
        if (log_fd >= 0) {
            sceIoLseek(log_fd, last_off, SCE_SEEK_SET);
            sceIoWrite(log_fd, line, n);
        }
    } else {
        repeats = 1;
        last_ts = sceKernelGetProcessTimeWide() / 1000;
        snprintf(last_body, sizeof(last_body), "%s", body);
        n = snprintf(line, sizeof(line), "[%llu] %s\n", last_ts, body);
        sceClibPrintf("%s", line);
        // Sin buffer: cada línea llega al archivo aunque el proceso muera justo después.
        if (log_fd >= 0) {
            last_off = sceIoLseek(log_fd, 0, SCE_SEEK_END);
            sceIoWrite(log_fd, line, n);
        }
    }
    pthread_mutex_unlock(&log_lock);
}

void l_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog("I", fmt, ap);
    va_end(ap);
}

void l_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog("E", fmt, ap);
    va_end(ap);
}

void fatal_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog("FATAL", fmt, ap);
    va_end(ap);
    sceKernelDelayThread(3 * 1000 * 1000);
    sceKernelExitProcess(0);
    while (1) {}
}
