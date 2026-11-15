# Inotia 2 — Progreso del Port (iOS Mach-O)

Detalle y arquitectura en `PORTING_PLAN.md`.

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
  - [x] **Fix Pantalla Negra a 0 FPS (log_003)**:
    1. **vitaGL vendorizada** (`vendor/vitaGL` heredada de Zenonia 3/4 y Prince of Persia Classic):
       el `libvitaGL.a` prebuilt del SDK vitasdk no definía `HAVE_SOFTFP_ABI` (`SOFTFP_ABI=1`). En ARM
       softfp, `sceGxmSetViewport` recibía los argumentos flotantes en registros enteros de ARM en vez
       de registros VFP (`s0..s5`), destruyendo la matriz de viewport de hardware (ancho/alto 0).
       Con `SOFTFP_ABI=1`, vitaGL activa el trampoline `sceGxmSetViewport_sfp` que traslada los floats
       a VFP antes de saltar a libgxm.
    2. **`NO_SPLASHSCREEN=1` (`SKIP_SPLASHSCREEN`)**: en vitaGL de SDK, el splashscreen animado queda
       activo y `vglSwapBuffers` hace `if (is_splashscreen_active) return;`, congelando el swap a 0 FPS.
       Se eliminó el splashscreen y se añadieron stubs limpios (`source/vitagl_no_splash.c`).
    3. **`system_app_mode` deshabilitado** (`#if 0` en `source/gxm.c`): previene que vitaGL desvíe los
       buffers a `sceSharedFbEnd` creyendo ser app de sistema.
    4. **Client States en `gles_present`**: se fuerza `glEnableClientState(GL_VERTEX_ARRAY)` y
       `glDisableClientState(GL_COLOR_ARRAY / GL_NORMAL_ARRAY)` durante el renderizado del quad a pantalla,
       evitando que arrays de vértices o colores del juego corrompan la presentación, y restaurando el
       estado intacto al terminar.
    5. **Conteo de draw calls**: se registra el número de `glDrawArrays` emitidos en cada frame en el log.
  - [x] **GATE consola ✅**: ¡Title screen visible en pantalla con audio activo y draws funcionando!
- [x] **3. Input** — [x] touch (multitoque, pad virtual), [x] botones físicos (D-Pad y Cruz / X funcionales in-game; botones restantes Square, Triangle, Circle, Triggers en progreso).
  - GATE consola ✅: ¡Juego jugable! Movimiento con cruceta y confirmación/ataque con Cruz funcionales.
- [x] **4. Texto** — [x] FreeType sobre el atlas 512×32 del motor (fuentes `sa0:data/font/pvf/`
  ltn0/kr0/jpn0, o `ux0:data/inotia2/font.ttf` primero si existe), [x] IME para el nombre.
  - GATE consola ✅: Texto y diálogos perfectamente legibles in-game.
- [x] **5. Audio** — [x] RemoteIO/AudioSession → `sceAudioOut` (formato del ASBD de FMOD: 24000 Hz, 2ch s16),
  [x] `.fev` desde el bundle + media path.
  - [x] **Fix crash #4 (dump `0x0009d0361f`, PC `pthread_detach`)**: al terminar un stream de audio en
    el Title, `FMOD_OS_Thread_Destroy` desreferencia el puntero de thread y pasa el primer campo de
    `pte_thread_t` (que en la Vita es el `SceUID` `0x4001019d`) a `pthread_detach`. `pthread_detach` de
    newlib asume que es un puntero a estructura y crashea con Data Abort al hacer `ldr r3, [r4, #28]`.
    Fix: `d_pthread_detach` seguro que reconoce `SceUID`s (0x4001xxxx) e ignora/maneja limpiamente
    cualquier handle no-puntero sin crashear. Además `d_pthread_self` devuelve `sceKernelGetThreadId()`
    si no está en el pool de pthread, y `sem_*` validan punteros nulos.
  - GATE consola ✅: Música del título y SFX verificados.
- [ ] **6. Video** — [x] se saltea (dispara el fin de video del motor); [ ] SceAvPlayer real.
- [x] **7. Guardado y Sistema** — [x] Guardado en `saves/` y selección de slots funcionando, [x] tienda/red/más juegos offline,
  [x] LiveArea completa con imágenes del juego, [ ] suspensión (`pauseClet`/`resumeClet`).

## Resumen de Bloqueadores Resueltos hacia Jugabilidad

- [x] **Fix crash #5 / #6 (`_LoadString` opcode `0x6820`)**:
  - Parche manual de desvío de cadena en `macho.c` sobrescribía un opcode de carga de registro en vez de la llamada. Corregido para restaurar la instrucción original intacta.
- [x] **Fix crash #7 (dump `0x000d692877`, Data Abort en `_DlgProc_Init` `0x9800efa8`)**:
  - Al seleccionar slot para iniciar juego, `_DlgProc_Init` cargaba `*0x981aab3c` y escribía en `0x00206a04` (`_DlgProc_nState`) sin rebasear, provocando aborto de datos.
  - Causa raíz: `macho_prep.py` descartaba el símbolo completo por tener datos no-puntero adyacentes (`owner_bad > owner_ok`), dejando 105 punteros críticos en `__data` sin rebase.
  - Solución: `macho_prep.py` ahora conserva de forma estricta todo destino exacto a función o símbolo (`func`, `sym`, `cstr`), y `source/macho.c` aplica una lista de salvaguarda `extra_rebases[]` garantizando rebase automático en memoria.
- [x] **HITO ALCANZADO: ¡EL JUEGO ES JUGABLE EN HARDWARE REAL!**


## Idiomas ES/PT + menú de opciones (2026-11-13, sin probar en consola)

- [x] **Formato de textos**: todo el texto del juego (menús, objetos, habilidades, misiones, ayuda,
  diálogos) está en `strindex.dat` (u32 BE n + n×{name[32], u32 LE inicio, u32 LE fin}) +
  `strdata.dat` (UTF-8 con NUL). No es el `memorytext` de Inotia 4. `LoadString` busca cada tabla por
  nombre y copia `fin-inicio` bytes al heap de medios (`SetMediaSize`, ~7 MB libres): el largo es libre.
  El motor avanza UTF-8 bien (`Str_Split`/`Text_*` miran 0xC0/0xE0/0xF0) y FreeType dibuja acentos:
  no hace falta plegar a ASCII como en Zenonia 2. No hay textos de UI en el binario fuera de esas tablas
  (solo imágenes: logo, agree_en.png).
- [x] **Redirección** (`hle_cs.c`, `CS_knlGetResourceID`): solo `strindex.dat`/`strdata.dat` se sirven
  desde `ux0:data/inotia2/lang/{spa,por}/` si existen; si no, inglés del bundle (que nunca se toca).
- [x] **Cambio en vivo**: al cerrar el menú con otro idioma se llama otra vez a `_LoadString` (reusa o
  realoja cada string con `SetMediaSize`). Lo ya armado en pantalla (diálogo abierto) queda en el idioma
  anterior hasta reabrirlo.
- [x] **Menú START+SELECT** (`controls.c`, juego en pausa, overlay 960×544 con FreeType sobre el último
  frame en `gles_present`): reasignar Ataque/OK, Habilidad 1–4, Objeto, Menú, Mapa (un botón por acción,
  intercambio si ya está usado), idioma, valores por defecto. Persistencia en `controls.txt`
  (`ACCION = BOTON`, `LANG = EN|ES|PT`). START/SELECT tocan al soltar (toque de 100 ms) para que el combo
  no abra el menú del juego.
- [x] **Mejoras Gráficas y Rendimiento (como Zenonia 2 y Destinia)**:
  - **Filtros Gráficos**: Sharp Pixel-Art (Nearest-neighbor 2x), Smooth Bilinear, Vibrant OLED Crisp (+15% brillo/contraste), Vibrant OLED Smooth.
  - **Relación de Aspecto (Aspect Ratio)**: 3:2 Fit nativo (816×544 con bandas laterales limpias), Fullscreen Stretch (960×544), Integer 1x Centrado (480×320). La calibración táctil (`to_game` en `input.c`) escala dinámicamente según el viewport activo.
  - **Optimizaciones de FPS**: selector entre 30 FPS (Battery Saver, `eglSwapInterval(2)`) y 60 FPS / ORIGINAL (`eglSwapInterval(1)`).
  - **Atajos rápidos (Hotkeys)**:
    - `Select + R`: alternar entre 30 FPS y 60 FPS.
    - `L + R + Triangle`: rotar filtros gráficos.
    - `L + R + Square`: rotar modo de aspecto.
  - **Cierre de menú con START**:
    - En el menú del port (START+SELECT): tanto **START** como **Círculo** guardan y cierran el menú.
    - En el menú del juego (menú dragón): al pulsar **START** con el menú abierto (botón dragón 0x64 oculto), se activa automáticamente el botón Salir / Exit (`MMrect2[3]` en coordenadas `x=242, y=279`), cerrando el menú de inmediato sin necesidad de tocar la pantalla táctil.
  - **Persistencia**: `controls.txt` almacena `FPS = 30|ORIGINAL`, `FILTER = N` y `ASPECT = N`.
- [x] **Herramienta** `scripts/inotia2_lang.py` (`export|build|check es|pt`, `test` = reempaquetado
  idéntico byte a byte). Plantillas en `inotia2_translation/{es,pt}/NN_<grupo>.json` (no van a git);
  glosario de nombres en `inotia2_translation/_glossary_{es,pt}.json`. Paquete:
  `build/inotia2_vita_traduccion_ES_PT.zip` (`inotia2/lang/{spa,por}/`, se extrae en `ux0:data/`).
- [x] **Traducido completo (ES y PT, 3498/3498 cadenas cada uno, 0 rechazadas por `check`)**: glosario de
  nombres (1591: objetos, habilidades, monstruos, NPC, mapas, clases, títulos de misiones, tiendas), UI/sistema
  (TextStr, Symbol, WV_CONST, opciones, popups), ayuda, descripciones de objetos/habilidades/clases, carteles,
  NPC, NScroll, misiones (Begin/EndDesc) y todo el diálogo de historia (EvtDlg, DlgProc, DlgSel). Nombres propios
  de personajes y lugares quedan como en inglés (Bronzebeard, Goldhammer, Stonehammer, Luone...). El texto
  traducido quedó algo más corto que el inglés (ES 170 KB / PT 167 KB vs 174 KB).
- El checker de tokens ya no toma "5% of" como printf (sin flag de espacio): solo `%d`, `%s`, `%02d`, `%%`...
- [ ] **Probar en consola**: abrir/cerrar menú, reasignar, cambiar idioma en el título y en partida,
  diálogos largos (que los textos más largos no desborden cajas), nombres de objetos en tienda/inventario.
