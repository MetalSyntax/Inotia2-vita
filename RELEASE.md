# Inotia 2: A Wanderer of Luone (PS Vita) — Beta Release v1.2

## 📦 Release Package Contents

The release distribution includes:
1. **`inotia2.vpk`**: PlayStation Vita application package containing the native Mach-O loader, hardware-accelerated OpenGL ES 1.1 via vitaGL, FreeType font rendering, and physical/touch controls integration.
2. **`rebase.bin`**: Binary relocation table (9,314 absolute 32-bit memory pointers) generated specifically for the iOS ARMv6 executable of *Inotia 2* (version 1.1.41).

---

## 🛠️ What is `rebase.bin` and How is it Created?

### Why is `rebase.bin` Required?

1. **Memory Architecture Constraints**:
   * On iOS 3 (iPhone OS), the official executable (`Inotia2`) was compiled as a non-PIE (`MH_EXECUTE`) binary hard-linked to virtual base address `0x00001000`.
   * On PlayStation Vita OS, the entire address space below `0x81000000` is reserved for the kernel and cannot be mapped by userland applications (`kuKernelAllocMemBlock`).
   * Therefore, the loader shifts the entire Mach-O image into high memory by adding a constant offset:
     $$\text{MACHO\_SLIDE} = \text{0x98000000}$$

2. **Absence of Local Relocation Tables in App Store Binaries**:
   * When building release binaries for the App Store, Apple strips local relocation records (`nlocrel = 0`).
   * While ARMv6 machine code in the text segment primarily relies on PC-relative addressing (`ldr r0, [pc, #offset]`), the **data segments (`__DATA`, `__const`)** contain thousands of absolute 32-bit pointers pointing to C++ vtables, internal engine structures, FMOD sound callbacks, and string literals.
   * Without sliding these pointers, whenever the CPU attempts to call a function pointer or dereference an internal table, it branches to unmapped memory (`0x0001XXXX`), causing an immediate crash via `Prefetch Abort` or `Data Abort` (error `0x80010016` or `.psp2dmp` crash dump).

3. **The Role of `rebase.bin`**:
   * It provides a compact, high-speed lookup table telling the loader exactly which memory addresses in the binary need to have `MACHO_SLIDE` added during startup.

### Binary Structure of `rebase.bin`:
* **Offset 0x00 (4 bytes)**: Magic header `'RBS1'` (`0x31534252`).
* **Offset 0x04 (4 bytes)**: Total count ($N$) of 32-bit words to relocate (9,314 words).
* **Offset 0x08 ($N \times 4$ bytes)**: Sequential array of 32-bit virtual addresses (`vmaddr`) where absolute pointers reside.

### Step-by-Step Generation of `rebase.bin`

`rebase.bin` is generated using the [`tools/macho_prep.py`](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Inotia2-vita/tools/macho_prep.py) script included in the repository:

```bash
# Prerequisites: Python 3
python3 -I tools/macho_prep.py <path/to/Inotia2> <path/to/output/rebase.bin> [--report report.txt]

# Example used in the repository:
python3 -I tools/macho_prep.py ux0_data/inotia2/Inotia2.app/Inotia2 ux0_data/inotia2/rebase.bin --report ux0_data/macho_report.txt
```

Alternatively, use the automated staging pipeline:
```bash
tools/stage_data.sh /path/to/Payload/Inotia2.app
```

#### How `macho_prep.py` Operates Internally:
1. **Parses Mach-O Headers**: Decodes `LC_SEGMENT`, `LC_SYMTAB`, and `LC_DYSYMTAB` load commands.
2. **Reconstructs the Symbol Map**: Because the official binary was not stripped of internal symbol names, it extracts all functions, data labels, and string literals (`__cstring`) alongside their original virtual addresses.
3. **Heuristic Pointer Scanning**:
   * Scans all 32-bit words across `__DATA` and `__TEXT` (excluding dynamic symbol stubs resolved at runtime).
   * Verifies if each candidate points to a valid function entry point (taking the Thumb `+1` bit into account), a null-terminated string, or an object in the symbol table.
   * Discards accidental integer/text table matches using object-owner density heuristics.
4. **Outputs `rebase.bin`**: Writes the sorted list of valid addresses into the binary file.

---

## 🚀 Player Installation Guide

1. **PS Vita Requirements**:
   * Custom Firmware (Enso 3.60 / 3.65 recommended).
   * `kubridge.skprx` kernel plugin active under `*KERNEL` in `ur0:tai/config.txt`.
   * `libshacccg.suprx` runtime present in `ur0:data/` (obtainable via ShaRKBR33D).

2. **Game Assets Setup**:
   * Extract `Payload/Inotia2.app/` from your decrypted iOS 1.1.41 `.ipa`.
   * Copy the `Inotia2.app/` folder to `ux0:data/inotia2/Inotia2.app/`.
   * Copy `rebase.bin` to `ux0:data/inotia2/rebase.bin`.
   * Required directory layout on the Vita:
     ```text
     ux0:data/inotia2/
     ├── Inotia2.app/
     │   ├── Inotia2 (original iOS binary)
     │   ├── Inotia.fev
     │   ├── Inotia_bank00.fsb
     │   ├── Inotia_bank01.fsb
     │   ├── *.png, *.dat, *.c2e, etc.
     ├── rebase.bin
     ├── saves/ (created automatically on first launch)
     └── logs/  (created automatically for diagnostic logs)
     ```

3. **Install the VPK**:
   * Transfer and install `inotia2.vpk` using VitaShell.

---

## 🎮 Controls

| PS Vita Input | Virtual / In-Game Action | Status |
| :--- | :--- | :---: |
| **Front Touchscreen** | Full touch interface available at all times | ✅ Working |
| **D-Pad / Left Stick** | Character movement & menu navigation | ✅ Working |
| **Cross (✖)** | Attack / Primary Action / Dialogue Confirm | ✅ Working |
| **Square (◼)** | Skill 1 (Fireball / Lower skill slot) | ✅ Working |
| **Triangle (▲)** | Skill 2 (Middle skill slot) | ✅ Working |
| **Circle (●)** | Skill 3 (Upper skill slot) | ✅ Working |
| **L Trigger** | Potion / Quick Item slot | ✅ Working |
| **R Trigger** | Skill Wheel Toggle / Special Action | ✅ Working |
| **Start** | Inventory & Status Menu (opens and closes cleanly) | ✅ Working |
| **Select** | Minimap & World Map | ✅ Working |
| **Start + Select** | Port options & enhancements menu (game paused) | ✅ Working |
| **L + R** | Quick Hotkey: Toggle Virtual Buttons / HUD (0% opacity) | ✅ Working |
| **Select + R** | Quick Hotkey: Cycle FPS mode (20 FPS ↔ 30 FPS ↔ 60 FPS) | ✅ Working |
| **L + R + Triangle** | Quick Hotkey: Cycle Graphics Filter | ✅ Working |
| **L + R + Square** | Quick Hotkey: Cycle Aspect Ratio Mode | ✅ Working |

---

## 📋 Changelog (v1.2-beta)

* **In-Game Menu Close via START**: Pressing **START** now cleanly opens AND closes the in-game dragon / inventory menu (restoring game state and playing the close sound). In the port options menu (**Start + Select**), both **START** and **Circle** save and close.
* **Virtual Buttons / Clean HUD (0% Opacity)**: Added ability to completely hide on-screen buttons, skills, and D-pad with 0% opacity via runtime hook (`_VP_drawObjects`). Press **L + R** together to toggle a clean HUD. Physical buttons and touchscreen areas remain 100% active.
* **Graphics Enhancer Filters**: Real-time post-processing shaders:
  - `Sharp Pixel-Art 2x (Crisp)` (Nearest-neighbor 2x).
  - `Smooth Bilinear 2x` (Bilinear filtering).
  - `Vibrant OLED/LCD (Crisp)` (Sharp pixels + 15% color vibrancy and contrast boost).
  - `Vibrant OLED/LCD (Smooth)` (Smooth bilinear + 15% color vibrancy and contrast boost).
  - Quick hotkey `L + R + Triangle` to cycle filters anytime during gameplay.
* **Aspect Ratio Modes**: Selectable aspect ratios preserving proper geometry or utilizing the full Vita display:
  - `Aspect Ratio 3:2 Fit (816x544)`: Original iOS pixel geometry with minimal side borders.
  - `Fullscreen Stretch (960x544)`: Stretched across the 16:9 Vita screen.
  - `Integer 1x Center (480x320)`: 1x native pixel resolution.
  - Touchscreen coordinates dynamically scale and match each aspect mode.
  - Quick hotkey `L + R + Square` to cycle aspect ratio.
* **Port Options & Button Remapping Menu**: Access via **Start + Select** (pauses the game). Allows live button remapping, audio/language selection, HUD visibility, filter, and aspect ratio. Saved to `ux0:data/inotia2/controls.txt`.
* **Full Spanish & Portuguese Localization Support**: Ready for translated text packages (`lang/spa/` and `lang/por/`) with on-the-fly language switching.
* **Target Frame Rate (UI Mockup)**: Added frame rate selector in options menu and `Select + R` hotkey (`20 FPS (ORIGINAL)`, `30 FPS (Mockup)`, `60 FPS (Mockup)`). *Note: The game engine natively runs at 20 FPS fixed ticks; higher framerate modes are experimental UI placeholders.*

## 📋 Changelog (v1.1-beta)

* **Sound Fixed**: FMOD now finds `Inotia.fev` (the media path was being duplicated), and its VFPv2 vector-mode mixers/resampler, which crashed on the Vita's Cortex-A9, are replaced with scalar versions. BGM and sound effects play in menus and in-game.
* **Full Screen**: the image is stretched to the full 960×544 screen (no more side bars); touch mapping updated to match.
* **Settings Persist**: control type, auto-save and other options are now saved correctly (`config1.sav` was opened in append mode and reset on every launch). Expect one last reset of your options on the first launch of this version.
* **All Controls Mapped**: L/R triggers, Start and Select now work alongside the D-Pad, stick and face buttons.

## 📋 Changelog (v1.0-beta)

* **Damage Black Flash Fixed**: Corrected texture modulation in vitaGL during damage overlay rendering so the red flash displays without blacking out the screen.
* **Physical Control Mapping**: Full mapping of PlayStation Vita hardware buttons to the virtual HUD touch points.
* **Performance Enhancements**: Optimized vertex array float conversion loops, delivering a steady 20–30 FPS in gameplay.
* **CoreAudio / FMOD Integration**: Added BGM port volume initialization and bundle path asset redirection.
