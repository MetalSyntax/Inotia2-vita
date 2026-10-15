#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdarg.h>
#include <stdio.h>

#include "log.h"

static SceUID log_fd = -1;

void log_open(const char *path) {
    log_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
}

static void vlog(const char *tag, const char *fmt, va_list ap) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[%llu] %s ", sceKernelGetProcessTimeWide() / 1000, tag);
    n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
    if (n > (int)sizeof(buf) - 2)
        n = sizeof(buf) - 2;
    buf[n++] = '\n';
    buf[n] = '\0';
    sceClibPrintf("%s", buf);
    // Sin buffer: cada línea llega al archivo aunque el proceso muera justo después.
    if (log_fd >= 0)
        sceIoWrite(log_fd, buf, n);
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
