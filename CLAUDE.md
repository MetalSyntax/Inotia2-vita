# Inotia2-vita

Port de Inotia 2 (iOS, Mach-O armv6) a PS Vita con un **loader de Mach-O** propio (no soloader:
no hay ELF). Leer `PORTING_PLAN.md` antes de tocar nada.

## Estructura

- `source/macho.c`: carga, rebase (`rebase.bin`), imports, hooks, `call_guest`.
- `source/darwin.c`: libc/libgcc/pthread de Darwin. `source/hle_cs.c`: capa `CS_*`/`MC_fs*` de Com2uS.
- `source/objc.c`: mini runtime ObjC por nombre de selector (texto FreeType, NSBundle, IME).
  `source/gles.c`: GLES1→vitaGL (FBO 320×480 rotado). `source/coreaudio.c`: AudioUnit→sceAudioOut.
  `source/input.c`: touch/botones.
- En la Vita: `ux0:data/inotia2/{Inotia2.app/, rebase.bin, saves/, logs/}` (sandbox de iOS).
  `tools/stage_data.sh` lo arma; `tools/deploy_data.py` lo sube por FTP (`--logs` baja log y dumps).
- `source/asm.S`: thunk de imports faltantes y `call_guest` (preserva r9).
- `tools/macho_prep.py`: genera `rebase.bin` + reporte de imports. `tools/annotate.py <bin> <símbolo>`:
  desensamblado con selectores/clases/cadenas resueltos. `game_app` → la `.app` del IPA (symlink).
- Referencia de motor: `../Inotia3-vita` (mismo engine en Android, ya jugable).

## Reglas

- ABI iOS: softfp (`SOFTFP` en `imports.h` para todo lo que tenga float), r9 temporal (todo se
  compila con `-ffixed-r9`; callbacks desde newlib → `call_guest`), **int64 sin alinear a par de
  registros** (partir en dos `uint32_t`).
- **Pila**: iOS la alinea a 4, la Vita asume 8. Todo lo que el juego llama de nuestro lado entra por
  `ml_align_call` (lo hacen solos `resolve()` y `macho_hook`); imports que son variables → `V(...)`.
- **Variádicos**: palabras de 32 bits sin alinear (double/long long en 2 palabras): nada de pasar
  argumentos del juego a `printf` de newlib; usar `darwin_vformat` (`darwin_printf.c`).
- Pruebas en el host: `tools/cgbi_check.py`, `tools/printf_check.py`.
- Tablas de imports: `F(__divsi3, fn)` registra `___divsi3` (el macro agrega el `_` de Mach-O).
- Hooks solo por nombre (`macho_hook("_CS_fsOpen", ...)`): el binario no tiene strip. Verificar la
  semántica con `annotate.py` antes de reemplazar una función.
- Crash: PC − `0x98000000` = dirección en el Mach-O (`otool -tV`/`annotate.py`).
- Nada del IPA al repo (`.gitignore`). Un bug por corrida de consola, guiado por `logs/log_NNN.log`.
