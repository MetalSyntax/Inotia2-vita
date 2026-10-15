# Inotia2-vita

Port de Inotia 2 (iOS, Mach-O armv6) a PS Vita con un **loader de Mach-O** propio (no soloader:
no hay ELF). Leer `PORTING_PLAN.md` antes de tocar nada.

## Estructura

- `source/macho.c`: carga, rebase (`rebase.bin`), imports, hooks, `call_guest`.
- `source/darwin.c`: libc/libgcc/pthread de Darwin. `source/hle_cs.c`: capa `CS_*` de Com2uS.
- `source/asm.S`: thunk de imports faltantes y `call_guest` (preserva r9).
- `tools/macho_prep.py`: genera `rebase.bin` + reporte de imports. `tools/annotate.py <bin> <símbolo>`:
  desensamblado con selectores/clases/cadenas resueltos. `game_app` → la `.app` del IPA (symlink).
- Referencia de motor: `../Inotia3-vita` (mismo engine en Android, ya jugable).

## Reglas

- ABI iOS: softfp (`SOFTFP` en `imports.h` para todo lo que tenga float), r9 temporal (todo se
  compila con `-ffixed-r9`; callbacks desde newlib → `call_guest`), **int64 sin alinear a par de
  registros** (partir en dos `uint32_t`).
- Hooks solo por nombre (`macho_hook("_CS_fsOpen", ...)`): el binario no tiene strip. Verificar la
  semántica con `annotate.py` antes de reemplazar una función.
- Crash: PC − `0x98000000` = dirección en el Mach-O (`otool -tV`/`annotate.py`).
- Nada del IPA al repo (`.gitignore`). Un bug por corrida de consola, guiado por `log.txt`.
