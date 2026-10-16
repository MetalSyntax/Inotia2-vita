# Inotia 2: A Wanderer of Luone (PS Vita) — Beta Release v1.0

## 📦 Contenido de la Release

El paquete de lanzamiento incluye:
1. **`inotia2.vpk`**: Instalable para PlayStation Vita con el loader Mach-O nativo, compatibilidad OpenGL ES 1.1 vía vitaGL, FreeType y soporte de controles físicos y táctiles.
2. **`rebase.bin`**: Tabla binaria de reubicaciones (9.314 direcciones de memoria absoluta) generada específicamente para el ejecutable iOS ARMv6 de *Inotia 2* (versión 1.1.41).

---

## 🛠️ ¿Qué es y Cómo se Crea `rebase.bin`?

### ¿Por qué existe `rebase.bin`?
1. **Diferencia de arquitectura de memoria**:
   * En iOS 3 (iPhone OS), los ejecutables no-PIE (`MH_EXECUTE`) se enlazaban para cargarse en la dirección virtual fija `0x00001000`.
   * En la PlayStation Vita, el rango de direcciones por debajo de `0x81000000` está reservado por el kernel de PlayStation Vita OS y no puede ser reservado por procesos de usuario (`kuKernelAllocMemBlock`).
   * Por tanto, el loader traslada todo el binario de iOS a memoria alta aplicando un desplazamiento constante:
     $$\text{MACHO\_SLIDE} = \text{0x98000000}$$
2. **Falta de reubicaciones locales en el binario original**:
   * Apple elimina las tablas de reubicación local (`nlocrel = 0`) al empaquetar aplicaciones para la App Store.
   * Aunque el código ejecutable ARMv6 suele usar direccionamiento relativo al PC (`ldr r0, [pc, #offset]`), los **segmentos de datos (`__DATA`, `__const`)** contienen miles de punteros absolutos de 32 bits a vtables de C++, estructuras del motor, callbacks de FMOD y cadenas de texto.
   * Sin modificar esos punteros, al intentar acceder a una función o tabla, la CPU saltaría a `0x0001XXXX` (memoria no asignada), provocando un cierre inmediato por `Prefetch Abort` o `Data Abort` (código de error `0x80010016` o crash dump `.psp2dmp`).
3. **El rol de `rebase.bin`**:
   * Es un archivo binario simple y ultrarrápido que le indica al loader exactamente en qué direcciones de memoria debe sumar `MACHO_SLIDE` al arrancar el juego.

### Formato de `rebase.bin`:
* **Offset 0x00 (4 bytes)**: Magic `'RBS1'` (`0x31534252`).
* **Offset 0x04 (4 bytes)**: Número total de palabras de 32 bits a reubicar ($N$).
* **Offset 0x08 ($N \times 4$ bytes)**: Lista secuencial de direcciones virtuales (vmaddr) donde residen los punteros absolutos que deben sumarse con `MACHO_SLIDE`.

### ¿Cómo se Genera `rebase.bin` paso a paso?
Se utiliza el script `tools/macho_prep.py` incluido en el repositorio:

```bash
# 1. Asegúrate de tener Python 3 instalado
python3 -I tools/macho_prep.py <ruta/al/binario/Inotia2> <ruta/de/salida/rebase.bin> [--report reporte.txt]

# Ejemplo real usado en el proyecto:
python3 -I tools/macho_prep.py ux0_data/inotia2/Inotia2.app/Inotia2 ux0_data/inotia2/rebase.bin --report ux0_data/macho_report.txt
```

O utilizando el script automatizado:
```bash
tools/stage_data.sh /ruta/a/Inotia2.app
```

#### ¿Qué hace internamente `macho_prep.py`?
1. **Analiza las cabeceras Mach-O**: Lee los comandos de carga `LC_SEGMENT`, `LC_SYMTAB` y `LC_DYSYMTAB`.
2. **Reconstruye el mapa de símbolos**: Como el ejecutable oficial no tiene strip de símbolos internos, extrae todas las funciones, tablas, cadenas (`__cstring`) y variables globales con sus direcciones reales.
3. **Escaneo heurístico de punteros**:
   * Recorre todas las palabras de 32 bits en los segmentos `__DATA` y `__TEXT` (excluyendo código ya resuelto por relocs dinámicas externas).
   * Valida si el valor de cada palabra apunta exactamente al inicio de una función válida (teniendo en cuenta el bit Thumb `+1`), una cadena terminada en nulo, o una variable definida en la tabla de símbolos.
   * Descarta tablas densas de enteros y datos falsos positivos mediante análisis de densidad por objeto propietario.
4. **Exporta el archivo final**: Empaqueta la lista ordenada de direcciones en `rebase.bin`.

---

## 🚀 Guía de Instalación para Usuarios

1. **Requisitos en la PS Vita**:
   * Custom Firmware (Enso 3.60 / 3.65 recomendado).
   * Plugin `kubridge.skprx` instalado bajo `*KERNEL` en `ur0:tai/config.txt`.
   * `libshacccg.suprx` instalado en `ur0:data/` (se obtiene con ShaRKBR33D).

2. **Instalación de archivos del juego**:
   * Copia la carpeta `Inotia2.app` (extraída del IPA de iOS 1.1.41) en `ux0:data/inotia2/Inotia2.app/`.
   * Copia el archivo `rebase.bin` en `ux0:data/inotia2/rebase.bin`.
   * Estructura requerida en la consola:
     ```text
     ux0:data/inotia2/
     ├── Inotia2.app/
     │   ├── Inotia2 (binario iOS)
     │   ├── Inotia.fev
     │   ├── Inotia_bank00.fsb
     │   ├── Inotia_bank01.fsb
     │   ├── *.png, *.dat, *.c2e, etc.
     ├── rebase.bin
     ├── saves/ (creada automáticamente al jugar)
     └── logs/  (creada automáticamente para diagnósticos)
     ```

3. **Instalación del VPK**:
   * Instala `inotia2.vpk` usando VitaShell.

---

## 🎮 Controles

| Botón PS Vita | Acción Virtual |
| :--- | :--- |
| **Cruceta / Stick Izquierdo** | Movimiento del personaje y navegación en menús |
| **Cruz (✖)** | Ataque / Acción principal / Confirmar diálogo |
| **Cuadrado (◼)** | Habilidad 1 (Fuego / Habilidad inferior) |
| **Triángulo (▲)** | Habilidad 2 (Habilidad media) |
| **Círculo (●)** | Habilidad 3 (Habilidad superior) |
| **Gatillo L1** | Poción / Acceso rápido de objeto |
| **Gatillo R1** | Alternar rueda de habilidades / Acción especial |
| **Start** | Menú de inventario / Estado (Icono espadas) |
| **Select** | Minimapa / Mapa general |
| **Pantalla Táctil** | Control táctil completo disponible en todo momento |

---

## 📋 Registro de Cambios (v1.0-beta)

* **Pantalla negra eliminada**: Se corrigió el problema de modulado de textura en vitaGL cuando el personaje recibía daño.
* **Mapeo de controles**: Los botones físicos de la consola se mapearon a los controles virtuales del juego.
* **Rendimiento**: Optimización en la conversión de arrays de vértices, logrando 20-30 FPS estables.
* **CoreAudio / FMOD**: Integración de llamadas de inicialización de volumen y resolución de rutas de paquetes de audio.
