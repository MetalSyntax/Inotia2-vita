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

## Estructura en la Vita (`ux0:data/inotia2/`, imita el sandbox de iOS)

```
ux0:data/inotia2/
  Inotia2.app/   bundle del IPA tal cual (ejecutable Inotia2 + recursos .c2e/.png/.fev/.fsb/.mp4)
  rebase.bin     generado por tools/macho_prep.py para ESE ejecutable
  saves/         partidas (CS_fs*/MC_fs*; el Documents/ de iOS); el deploy nunca toca su contenido
  logs/          log_001.log .. log_999.log, uno por corrida (después de 999 vuelve a 001)
```
`BUNDLE_PATH`/`DOCS_PATH`/`LOGS_PATH` en `source/hle.h`. Recursos: `CS_knlGetResource*` → `Inotia2.app/English.lproj/`
y `Inotia2.app/`; FMOD: `resourcePath` (objc.c) → `Inotia2.app/Inotia.fev` + media path para los `.fsb`.

## Arquitectura del loader

| Archivo | Qué reemplaza |
|---|---|
| `macho.c` / `asm.S` | dyld: carga, rebase, imports, hooks, `call_guest`/`call_guest6` (preservan r9) |
| `darwin.c` | libc/libgcc softfp/pthread/semáforos de Darwin |
| `objc.c` | mini runtime ObjC por **nombre de selector** (NSString, UIFont, NSBundle, NSCalendar, textInput*) + CoreGraphics del atlas de glifos (FreeType) + IME |
| `gles.c` | GLES1 → vitaGL. El juego dibuja en un FBO 320×480 (el renderbuffer vertical de EAGLView); `gles_present` lo rota/escala a 816×544 centrado |
| `coreaudio.c` | AudioUnit RemoteIO + AudioSession → hilo `sceAudioOut` que llama al render callback de FMOD |
| `hle_cs.c` | `CS_*`/`MC_fs*` (saves/), PNG (libpng en vez de UIImage), timers, FMOD, eventos que en iOS vuelven por ObjC (video, más juegos, música), anti-piratería, tienda y red offline |
| `input.c` | touch + botones → `VP_touchesProcess*` + `handleCletEvent` + `EVENT_POINT_*` (réplica de `-[EAGLView touches*]`) |

## Fases

- [x] **0. Repo y herramientas**: `macho_prep.py` (rebase + inventario de imports), `annotate.py`
  (desensamblado con selectores/clases/cadenas), `stage_data.sh`, `deploy_data.py` (FTP incremental).
- [x] **1. Loader mínimo** — GATE ✅ en consola (log_002): `startClet volvió; entrando al bucle de timers`.
  - [x] Carga en `0x98001000` (slide `0x98000000`: PC real − `0x98000000` = dirección en el Mach-O).
  - [x] Rebase, imports por tabla indirecta + relocs externas, thunks que registran lo que falta.
  - [x] **Fix crash #1** (dump `0x000e1d2ba5`, PC `0x1595e8` en `__symbol_stub4`): los stubs NO son
    PIC (`ldr ip,[pc]; ldr pc,[ip]; .long &la_ptr` absoluta) → sus 285 literales van a `rebase.bin`.
  - [x] **Fix**: los imports con `_` inicial en C (`__divsi3`, `__cxa_*`, softfp, SjLj, `*_chk`...)
    se registraban con un `_` de más y nunca resolvían (57 entradas).
  - [x] libc/libgcc/math/pthread/semáforos de Darwin (`darwin.c`).
  - [x] HLE: `CS_fs*`/`MC_fs*` (saves/), `CS_knlGetResource*` (bundle), timers, propiedades,
    Flurry, `VerifyApplication1`, versión (`objectForInfoDictionaryKey:`), fecha (NSCalendar).
  - [x] **Probado en consola** (log_002).
  - [x] **Fix crash #3** (dump `0x000b0525e9`, vitaGL `glColor4x`): la pila de iOS va alineada a 4 y
    el código de la Vita asume 8. Todo import/hook entra por `ml_align_call` (asm.S), que realinea
    copiando 16 palabras de argumentos de pila. También corrompía `%llu` en el log.
  - [x] printf de Darwin (`darwin_printf.c`): los variádicos de iOS son palabras sin alinear
    (double/long long en 2 palabras seguidas); `sprintf`/`vsnprintf`/`NSLog`/`stringWithFormat:`.
  - [x] El thunk de imports faltantes pasaba mal el índice: nunca se registraban los `unimpl:`.
  - [x] Imports de datos: un bloque por símbolo (antes uno por referencia: 392 copias).
- [x] **2. Render** — [x] vitaGL + FBO 320×480 + present rotado; [x] arrays `GL_FIXED`; [x] OES fb
  falsos; [x] PNG por libpng (premultiplicado + clave magenta, igual que el original). Los PNG del
  IPA son CgBI de Apple y se leen tal cual en la consola (`cgbi.c`, verificado en el host con
  `tools/cgbi_check.py`); los archivos del juego nunca se modifican.
  GATE consola: ✅ Verificado en consola — logo, menú de título, slots y gráficos in-game en orientación y escala correctas.
- [x] **3. Input** — [x] touch (multitoque, pad virtual), [x] botones físicos (D-Pad y Cross funcionales; botones restantes en mapeo/progreso).
  GATE consola: ✅ Verificado en consola — juego jugable, cruceta direccional y Cruz (X) operativos.
- [x] **4. Texto** — [x] FreeType sobre el atlas 512×32 del motor (fuentes `sa0:data/font/pvf/`
  ltn0/kr0/jpn0, o `ux0:data/inotia2/font.ttf` primero si existe), [x] IME para el nombre.
  GATE consola: ✅ Verificado en consola — texto legible en diálogos y menús.
- [x] **5. Audio** — [x] RemoteIO/AudioSession → `sceAudioOut` (formato del ASBD de FMOD),
  [x] `.fev` desde el bundle + media path.
  GATE consola: ✅ Verificado en consola — música de título y efectos activos.
- [ ] **6. Video** — [x] se saltea (dispara el fin de video del motor); [ ] SceAvPlayer real.
- [x] **7.** Guardado (saves/ funcionando, slots verificados), [x] tienda/red/más juegos offline,
  [x] LiveArea completa, [ ] suspensión (`pauseClet`/`resumeClet`), optimizaciones adicionales.

## Iterar en consola (un bug por corrida)

1. `tools/stage_data.sh` (solo si cambió el IPA o `macho_prep.py`), compilar, `python3 -I
   tools/deploy_data.py` (sube solo lo cambiado, migra la estructura vieja) + instalar el vpk
   (`psvita-toolkit deploy --vpk`) o `--eboot`.
2. Después de cada corrida: `python3 -I tools/deploy_data.py --logs` → `logs/` local (los log_NNN.log nuevos y los dumps de `ux0:data/`, que el sistema no deja
   mover a otra carpeta).
3. Crash con dump: `psvita-toolkit analyze logs/<dump> --elf build/inotia2`; si el PC está en
   `0x98xxxxxx`, restar `0x98000000` y mirar con `annotate.py`. Si el PC está en `__symbol_stub4`
   (`0x159140..0x159e9c`), `R12` es el slot del import.
4. En el log, `unimpl:` y `objc_msgSend(..., sel) -> nil` dicen qué falta implementar.

Requisitos en la Vita: `kubridge.skprx` en `ur0:tai/config.txt` (`*KERNEL`) y `libshacccg.suprx`
(ShaRKBR33D). Compilar: `cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Debug && cmake --build build`.
