#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/system_param.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include "gles.h"
#include "hle.h"
#include "log.h"
#include "macho.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;
// El juego corre en este hilo (en iOS, el hilo principal tiene 1 MB; se da margen).
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

// Un log por corrida: log_001.log .. log_999.log. El número sigue al del log escrito más
// recientemente (no al más alto), así después de 999 vuelve a 001 y va pisando los más viejos.
// Relanzar después de un crash no borra su log.
static int next_log_number(void) {
    sceIoMkdir(DATA_PATH "logs", 0777);
    int last = 0, n;
    uint64_t newest = 0;
    SceUID d = sceIoDopen(LOGS_PATH);
    SceIoDirent e;
    while (d >= 0 && sceIoDread(d, &e) > 0) {
        const SceDateTime *t = &e.d_stat.st_mtime;
        uint64_t key = (uint64_t)t->year << 48 | (uint64_t)t->month << 40 | (uint64_t)t->day << 32 |
                       (uint64_t)t->hour << 24 | t->minute << 16 | t->second << 8 | t->microsecond / 10000;
        if (sscanf(e.d_name, "log_%3d.log", &n) == 1 && n >= 1 && n <= 999 && key >= newest) {
            newest = key;
            last = n;
        }
    }
    if (d >= 0)
        sceIoDclose(d);
    return last % 999 + 1;
}

int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    sceIoMkdir(DATA_PATH, 0777);
    char log_path[128];
    snprintf(log_path, sizeof(log_path), LOGS_PATH "log_%03d.log", next_log_number());
    log_open(log_path);
    l_info("Inotia 2 loader (Mach-O) iniciando");

    if (sceIoGetstat("ur0:/data/libshacccg.suprx", &(SceIoStat){0}) < 0 &&
        sceIoGetstat("ur0:/data/external/libshacccg.suprx", &(SceIoStat){0}) < 0)
        fatal_error("Falta libshacccg.suprx (instalarlo con ShaRKBR33D)");
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);
    SceCommonDialogConfigParam dlg;
    sceCommonDialogConfigParamInit(&dlg);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int *)&dlg.language);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&dlg.enterButtonAssign);
    chdir(BUNDLE_PATH);
    gles_init();
    darwin_init();
    macho_load(BUNDLE_PATH "Inotia2", DATA_PATH "rebase.bin");
    hle_cs_install();
    input_init();
    macho_run_inits();

    uintptr_t start = macho_sym("_startClet");
    l_info("startClet en 0x%08X", (unsigned)(start - MACHO_SLIDE));
    call_guest((void *)start, 0, 0, 0, 0);
    l_info("startClet volvió; entrando al bucle de timers");

    for (;;) {
        input_poll();
        hle_frame();
        uint64_t wait = hle_run_timers();
        if (wait > 0)
            sceKernelDelayThread(wait > 1000 ? 1000 : (SceUInt)wait);
    }
}
