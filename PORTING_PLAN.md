# Inotia 2: A Wanderer of Luone (iOS 1.1.41) → PS Vita

Primer port de iOS. Plan general y RE de CoD WaW Zombies en `~/ios-vita-ports/PLAN.md`.

## Lo confirmado del binario (2026-10-14)

- `Inotia2`: Mach-O `MH_EXECUTE` thin **armv6**, **descifrado** (cryptid 0), **sin strip** (18.315
  símbolos), no PIE en `0x1000`, `nlocrel = 0`, dyld clásico (`LC_DYSYMTAB`, sin `LC_DYLD_INFO`).
- El código es **Thumb-1 PIC** (`ldr rX,[pc]; add rX,pc`): no lleva punteros absolutos. Solo los datos
  necesitan rebase → `tools/macho_prep.py` los encuentra validando contra la tabla de símbolos
  (8.897 palabras; 43 objetos descartados por ser tablas de texto/enteros).
- **Mismo motor que Inotia 3 Android** (capa WIPI de Com2uS: `CS_*`, `startClet/handleCletEvent`,
  wrapper GL `pact*`). La semántica de `CS_*` se toma de `../Inotia3-vita/decompiled_so/`.
- Pantalla lógica 480×320 apaisada (`CSX_WIPI_Initialize(320, 480)` + rotación).
- Bucle: no hay bucle propio; todo lo mueven timers (`CS_knlSetTimer` → `CFRunLoopTimer`) y
  `CS_grpFlushLcd` presenta cada frame (`[glView flushLCD]` + `FMOD_EventSystem_Update`).
- Audio: **FMOD Ex estático** sobre `AudioUnit` RemoteIO → emulando AudioUnit, FMOD decodifica
  los `.fsb` solo.
- Frontera con Apple (`tools/annotate.py` + análisis de llamadas): `CS_fs*`, `MC_fs*`, `CS_knl*`
  (recursos/timers/propiedades), `CS_net*`, `Store_*`, `Flurry_*`, `__util*` (fuentes vía
  CoreGraphics/UIKit), `CSX_fmod*`, salida de audio de FMOD, video (MPMoviePlayer).
- **ABI iOS ARMv6** (≠ AAPCS de la Vita): softfp (floats en registros enteros), **r9 temporal**
  (se compila con `-ffixed-r9` y `call_guest` lo preserva), y **los int64 no se alinean a par de
  registros** (`CS_knlSetTimer(t, int64 ms en r1:r2, param en r3)`).

## Fases

- [x] **0. Repo y herramientas**: `macho_prep.py` (rebase + inventario de imports), `annotate.py`
  (desensamblado con selectores/clases/cadenas), `stage_data.sh`.
- [ ] **1. Loader mínimo** — GATE: el log llega a `startClet volvió; entrando al bucle de timers`.
  - [x] Carga en `0x98001000` (slide `0x98000000`: PC real − `0x98000000` = dirección en el Mach-O).
  - [x] Rebase, imports por tabla indirecta + relocs externas, thunks que registran lo que falta.
  - [x] libc/libgcc/math/pthread/semáforos de Darwin (`darwin.c`).
  - [x] HLE: `CS_fs*` (Documents/), `CS_knlGetResource*` (bundle), timers, propiedades, Flurry.
  - [ ] **Probar en consola.**
- [ ] **2. Render** — vitaGL ES1: conversión `GL_FIXED` (copiar de Inotia3-vita `glutil.c`),
  `glGetFixedv`, `CS_grpFlushLcd` → `vglSwapBuffers`. GATE: logo en pantalla.
- [ ] **3. Input** — touch + botones → `VP_touchesProcessTouch` / `handleCletEvent` (réplica de
  `-[EAGLView touches*]`). GATE: menú navegable.
- [ ] **4. Texto** — `__util*` (fuentes CoreGraphics) → PGF del sistema o fuente propia.
- [ ] **5. Audio** — AudioUnit RemoteIO + AudioSession → `sceAudioOut`; `MC_fs*`/`FMOD_OS_File_*`.
- [ ] **6. Video** — `prologue.mp4`/`epilogue.mp4` con SceAvPlayer.
- [ ] **7. Guardado, tienda offline, LiveArea, rendimiento.**

## Cómo probar

```bash
tools/stage_data.sh                     # arma ux0_data/inotia2/ (recursos + Inotia2 + rebase.bin)
cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

En la Vita: instalar `build/inotia2.vpk`, copiar `ux0_data/inotia2/` a `ux0:data/inotia2/`, tener
`kubridge.skprx` en `ur0:tai/config.txt` (`*KERNEL`). Log: `ux0:data/inotia2/log.txt`.
