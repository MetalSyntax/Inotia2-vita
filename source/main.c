#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include "hle.h"
#include "log.h"
#include "macho.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;
// El juego corre en este hilo (en iOS, el hilo principal tiene 1 MB; se da margen).
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    sceIoMkdir(DATA_PATH, 0777);
    log_open(DATA_PATH "log.txt");
    l_info("Inotia 2 loader (Mach-O) iniciando");

    darwin_init();
    macho_load(DATA_PATH "Inotia2", DATA_PATH "rebase.bin");
    hle_cs_install();
    macho_run_inits();

    uintptr_t start = macho_sym("_startClet");
    l_info("startClet en 0x%08X", (unsigned)(start - MACHO_SLIDE));
    call_guest((void *)start, 0, 0, 0, 0);
    l_info("startClet volvió; entrando al bucle de timers");

    for (;;) {
        uint64_t wait = hle_run_timers();
        sceKernelDelayThread(wait > 1000 ? 1000 : (SceUInt)wait);
    }
}
