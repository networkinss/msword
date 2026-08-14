# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A native Windows x64 port of Microsoft Word for Windows 1.1a (historical
codename **Opus**). The original 16-bit C source and resources are compiled
and linked as-is; the port supplies only the platform work needed to run
that code safely on 64-bit Windows (segmented-memory emulation, Win16→Win32
API adaptation, translated assembly, modern file-format export). This is a
source-equivalent port, not a rewrite or emulator — **the original C source
in `src/Opus/` is the authoritative implementation and should not be
"modernized" or refactored** except where x64 correctness requires it.

Windows-only project; CMake refuses to configure on any other host
(`CMAKE_SIZEOF_VOID_P` must be 8, `WIN32` must be set).

### Reference: the pristine original source

`/home/amrit/IdeaProjects/MSWORD` (sibling directory, separate git repo) is
the unmodified Computer History Museum release of the Word for Windows
1.1a source that `src/Opus/`, `src/OpusEtAl/`, and `src/OpusProg/` were
seeded from — same layout, but with none of the x64-port edits applied.
Diff against it to see exactly what this port changed in a given original
file, e.g.:

```
diff -u /home/amrit/IdeaProjects/MSWORD/Opus/<file> src/Opus/<file>
```

This is the fastest way to tell whether a change in `src/Opus/` is a
deliberate port adaptation or an accidental behavior change — as of this
writing 282 files under `Opus/` differ from the pristine original.

## Build and run

From a PowerShell prompt, `cd src` first (the CMake project root is `src/`,
not the repo root):

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
& ..\bin\WORD1.exe
```

Use `x64-release` for an optimized build. Build a single target with
`cmake --build --preset x64-debug --target <name>` (e.g. `WORD1`,
`opus_original_engine`, `opus_x64_runtime`).

**Runtime dependency note**: `CMakeLists.txt` does not set
`CMAKE_MSVC_RUNTIME_LIBRARY`, so all targets link the dynamic MSVC CRT
(`/MD`) by default — `WORD1.exe` therefore depends on the Visual C++
Redistributable (VS 2022, x64) being present on the target machine, on top
of the core Windows DLLs it links directly (`user32`, `gdi32`, `ole32`,
`comdlg32`, `imm32`, `dbghelp`). To make the binary dependency-free on a
clean Windows 10/11 install, add
`set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")`
near the top of `src/CMakeLists.txt` and rebuild everything (the CRT choice
must be consistent across all linked objects); see `REQUIREMENTS.md` for
details.

Generated output locations (all git-ignored, created by CMake):
- `out/` — CMake cache and generated Visual Studio solution
  (`out\MicrosoftWordX64Port.sln`, startup project `WORD1`)
- `build/` — intermediate build tools, tests, probes, PDBs
- `bin/` — final `WORD1.exe` and runtime files

## Tests

```powershell
ctest --test-dir ..\out -C Debug --output-on-failure
```

(swap `Debug`→`Release` for a release build; run from `src/`, or use
`.\out` when invoking from the repo root). Run a single test with
`ctest --test-dir ..\out -C Debug -R <test-name> --output-on-failure`.

Test targets span: original data-structure/command-table unit tests
(`opus_original_sttb_test`, `opus_original_plc_test`,
`opus_original_strtbl_test`, `opus_original_command_test`), x64
runtime/format tests (`opus_x64_runtime_test`, `opus_sdm_cab_test`,
`opus_modern_formats_test`), a process-startup smoke test
(`word1_port_smoke_test`, runs `WORD1.exe --self-test`), and automated
end-to-end UI tests driven through `opus_word1_ui_test` against a live
`WORD1.exe` (typing, selection, formatting, clipboard, unicode, dialogs,
Save As, PDF export — see the `add_test(NAME opus_word1_*)` block at the
bottom of `src/CMakeLists.txt` for the full list and their CLI flags).

## Architecture

### Source layout

| Path | Contents |
| --- | --- |
| `src/Opus/` | Original Word/Opus application source and resources (C, `.des`/`.hs`/`.sdm` dialog descriptions, `.cur`/`.ico`/`.bmp`/`.dib` resources). Compiled into `opus_original_engine`. |
| `src/Opus/asm/` | Original 16-bit x86 assembly. **Inventoried but never compiled** — see "Legacy assembly" below. |
| `src/OpusEtAl/` | Original supporting tools/libraries/build inputs, including the historical build-time tool sources (`mkcmd.c`, `mkdlg.c`, `mergeelx.c`, `bitapp.c`) under `tools/src/`. |
| `src/OpusProg/` | Historical program documentation (reference only). |
| `src/port/original/` | The x64 compatibility layer: translated assembly routines (`opus_asm_*.cpp`), platform shims (`opus_win16_platform.cpp`, `opus_x64_heap.*`, `opus_x64_layout.*`), the modern-format export engine (`opus_modern_formats.cpp`), and all test/probe sources. |
| `src/port/tools/` | Native (x64) replacements for historical build-time tools whose original binaries/sources are missing from the archive (`opus_cabi_tool.cpp`, `opus_dibapp_tool.cpp`). |
| `src/cmake/` | CMake helper scripts that drive resource/source generation (e.g. `GenerateElxStid.ps1`, `GenerateMenuHelpHeader.cmake`). |

### The build-time code-generation pipeline

A large part of `src/CMakeLists.txt` reconstructs intermediate artifacts
that were produced by Microsoft's original toolchain but are not present in
the source archive (e.g. the Dialog Editor's `.des`→`.elx` compiler). Native
x64 tools stand in for these missing/16-bit build tools and are built
`EXCLUDE_FROM_ALL` early in configuration, then invoked via
`add_custom_command` to regenerate headers/tables consumed by
`opus_original_engine`:

- `opus_mkcmd_tool` (from `mkcmd.c`) — regenerates command tables
  (`opuscmd.h`, `OPUSCMD2.H`, `opuscmd.asm`, menu help, etc.) from
  `Opus/resource/*.cmd` and the reconstructed `.hs` dialog headers.
- `opus_mkdlg_tool` (from `mkdlg.c`) — regenerates `dlgcheck.h` (EL
  dialog range/parse tables) from `Opus/dlg/*.elx`.
- `opus_mergeelx_tool` (from `mergeelx.c`) — source for the fixed
  hidden-screen payload preserved as `elxinfo.h`.
- `opus_bitapp_tool` / `opus_dibapp_tool` — convert original `.cur`/`.ico`/
  `.bmp`/`.dib` resources into the byte-initializer headers the C source
  originally `#include`d (e.g. `SCREEN2.C`).
- `opus_cabi_tool` — computes native SDM CAB initializers from the
  reconstructed `port/original/*.hs` dialog headers.

All of this generated output lands under
`${CMAKE_CURRENT_BINARY_DIR}/generated/original` and is a required
dependency of `opus_original_engine`. If you add or rename a `.des`/`.hs`
dialog source, resource file, or `.cmd` table, expect these generation
steps to need updating too.

### Legacy assembly is inventoried, not compiled

`CMakeLists.txt` globs all `*.asm` under `src/Opus/asm/` into an
IDE-visible `legacy_sources` target and asserts the count is exactly **59**
files. This is a deliberate tripwire: it fails configuration if the
original assembly tree changes, forcing a conscious decision about whether
a new/removed `.asm` file needs a corresponding x64 translation. The actual
runtime behavior of that assembly lives as C/C++ translations in
`src/port/original/opus_asm_*.cpp` (e.g. `opus_asm_layout2.cpp`,
`opus_asm_search.cpp`, `opus_asm_wproc.cpp`), which are compiled into
`opus_original_engine` and `opus_x64_runtime` instead of the `.asm`.

### Compatibility layer conventions

- `port/original/opus_x64_compat.h` centralizes Win16→Win32 API shims (e.g.
  `GetProcAddress` wrapped so probing retired 16-bit modules like KERNEL/GDI
  by ordinal can't accidentally resolve an unrelated WORD1 export;
  `ChangeMenu` redirected to a native-width wrapper; `index` mapped to
  `strchr` for non-C++ TUs). Prefer extending this file over patching
  individual original source files when a Win16→Win32 API gap needs
  bridging.
- Original C translation units are compiled with `/J` (unsigned `char`,
  matching the original toolchain's default — required by the byte-oriented
  string/international-character routines) and must **not** be compiled as
  C++.
- `WIN`, `WIN23`, `CRLF`, `NONATIVE`, `OPUS_X64`, `NOMINMAX` are the
  standard compile definitions across original-source targets; match them
  when adding a new translation unit.
- Segmented/double-indirect Win16 memory handles are mapped onto a native
  x64-safe heap in `opus_x64_heap.*` / `opus_x64_layout.*`.

### Target graph

- `opus_original_engine` (static lib) — the actual Word application logic:
  every `.c` file here is original Opus source; only `port/original/opus_asm_*`
  and `opus_win95_chrome.cpp`/`opus_x64_segment_anchors.c` are port-added.
- `opus_x64_runtime` (static lib) — the platform/compat runtime linked
  under the engine: heap, layout, dispatch, SDM/CAB, and modern-format
  support. Built as C++20 with `/W4 /permissive- /guard:cf /sdl`.
- `WORD1` — the shipped executable. Its `WinMain` is the original Opus
  entry point; `port/original/opus_original_startup_probe.cpp` provides the
  Unicode-aware native entry adapter.
- `opus_original_startup_probe` — a resource-free diagnostic build of the
  same startup graph, useful as an all-symbols link gate independent of
  `WORD1`'s resources.
- Numerous `opus_*_test` executables registered via `ctest` (see Tests
  above).

### Modern format export

`port/original/opus_modern_formats.cpp` (~4200 lines) implements DOCX/PDF/
ODT/OOXML export/import support layered on top of the original document
engine — this is the one major area that is genuinely new functionality
rather than a straight port, and is covered by `opus_modern_formats_test`
and the `--save-as` / `--pdf-export` UI tests.

## Contributing conventions

- Preserve original Word behavior; keep all native interfaces pointer-width
  safe (no truncating 16-bit handles/pointers on x64).
- Prefer source-equivalent translations of historical routines over
  rewrites; isolate unavoidable Windows API adaptation at the port boundary
  (`opus_x64_compat.h`, `port/original/`) rather than scattering `#ifdef`s
  through original files.
- Add focused tests for newly translated behavior (extend the relevant
  `opus_*_test` target or `opus_word1_ui_test` flag rather than inventing a
  new harness).
