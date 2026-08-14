# TODO - mingw-w64 build path

Status as of **2026-08-14**. This file tracks the work to build the port with
mingw-w64 GCC instead of (or alongside) MSVC. MSVC remains the primary
supported compiler and is unaffected by everything below.

## Why this exists

The intended build machine (`m93p`) has **Visual Studio Build Tools 2019 with no
Windows SDK installed at all** - `C:\Program Files (x86)\Windows Kits\10`
contains only `Catalogs\` and `Redist\`, no `Include\` or `Lib\`. It also has no
CMake. Since `CMakePresets.json` requires the `Visual Studio 17 2022` generator,
that machine cannot currently configure or build this repository.

Two ways out: install VS Build Tools 2022 (~4 GB, no repo changes), or make the
tree buildable with mingw-w64 (small toolchain, no Windows SDK required, but
needs the work listed here). The mingw path was chosen to investigate first.

## Current state of the environments

**Linux workstation** (fanless, use freely for compile experiments):

| Tool | Version |
| --- | --- |
| `x86_64-w64-mingw32-gcc` / `g++` | GCC 10-posix 20220113 (thread model: posix) |
| `x86_64-w64-mingw32-windres` | GNU binutils 2.38 |
| CMake | 4.4.2 (Kitware APT repo) |
| Ninja | 1.10.1 |

**`m93p`** - Windows 10 21H2, build 19044.2364, German locale, at
`192.168.0.80`, reachable over SSH as `user` with key auth. Small desktop with
an audible fan: keep long CPU-heavy runs off it, or warn first.

- Working copy of this repo at **`C:\dev\msword`** (verified byte-identical to
  the Linux tree; `git status` matches exactly, no line-ending churn)
- MSYS2 **base only** at `C:\tools\msys64` (Chocolatey's prefix, not
  `C:\msys64`); `pacman` 6.1.0 works, **no compiler installed yet**
- VS Build Tools 2019 16.11.32901.82, MSVC v142 14.29.30133, MSBuild present
- No Windows SDK, no CMake, no Ninja
- Git for Windows present (its bundled `mingw64` is git's own runtime, not a
  usable compiler); Cygwin at `C:\cygwin64` - keep it off the build PATH
- `winget` source index is broken there (`0x8a15000f`); use `choco`

## Done in this session

Changes are confined to `src/CMakeLists.txt`, `src/CMakePresets.json`,
`README.md` and this file. **No original Opus source was modified.**

- `src/CMakeLists.txt`
  - added `OPUS_GNU_ORIGINAL_C_OPTIONS` (`-funsigned-char`) and
    `OPUS_GNU_UTF8_OPTIONS`, plus `else()` branches on **14** of the 16
    `if(MSVC)` option blocks. Previously a non-MSVC build silently dropped `/J`
    (see "unsigned char" below) - the two skipped blocks are `/W4`-only and
    have no GNU counterpart worth adding.
  - `_WIN32_WINNT` / `WINVER` / `NTDDI_VERSION` = Win10 for non-MSVC builds
  - static-runtime linking for non-MSVC (`-static -static-libgcc
    -static-libstdc++`) under the existing `MSWORD_STATIC_CRT` option
  - the hardcoded Windows SDK `Windows.h` lookup is now MSVC-only; non-MSVC
    resolves it with `find_file` over `CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES`
- `src/CMakePresets.json` - `x64-mingw-debug` / `x64-mingw-release`
  (Ninja Multi-Config, binary dir `out-mingw/` so the VS build in `out/` is
  untouched)

None of this has been configured or built yet, on either machine.

## Blockers found, not yet fixed

### 1. Flexible array member in a union - one line

`src/Opus/wordtech/disp.h:248`, inside `struct PLDR`:

```c
union   {
        HQ      hqpldre;    /* when fExternal true */
        struct DR rgdr[];   /* when fExternal false */
        };
```

MSVC accepts this; GCC rejects `[]` in a union outright, and `-fms-extensions`
does **not** help (verified - error count identical). GCC does accept a
zero-length array `rgdr[0]` there, which is size-preserving, so `cwPLDR` and the
struct layout are unchanged.

This single line accounts for **81** of the errors, because 81 translation units
include the header. Patching it in a scratch copy took the clean-compile count
from 18/130 to **41/130** and total errors from 575 to 494.

Suggested fix, guarded so MSVC keeps the original spelling:

```c
#ifdef __GNUC__
        struct DR rgdr[0];  /* GCC rejects [] inside a union; [0] is size-preserving */
#else
        struct DR rgdr[];
#endif
```

### 2. Cast-as-lvalue - 16 sites in 4 files

`*((TYPE *) p)++`, a K&R/MSVC extension GCC removed in 4.0:

| File | Sites |
| --- | --- |
| `src/Opus/style.c` | 6 |
| `src/OpusEtAl/tools/src/mkcmd.c` | 5 |
| `src/Opus/ffread.c` | 3 |
| `src/Opus/interp/exp.c` | 2 |

Mechanical rewrite (`p += sizeof(TYPE)` with a typed temporary). The `mkcmd.c`
ones block the tool from compiling at all, so they come first.

### 3. Host-tool bootstrap

Five build-time generators - `opus_mkcmd_tool`, `opus_mkdlg_tool`,
`opus_cabi_tool`, `opus_bitapp_tool`, `opus_dibapp_tool` - are declared as
ordinary `add_executable` targets and then executed during the build via
`COMMAND "$<TARGET_FILE:...>"`.

This is fine for a **native** mingw build on `m93p` (tools build and run on the
same machine) but fatal for **cross-compiling from Linux**: CMake would build
them as Windows `.exe` and then try to run them on Linux. Wine is ruled out.
Cross-compiling would require restructuring these five into a host sub-build
(`ExternalProject`, or a `-DOPUS_HOST_TOOLS_DIR` pointing at prebuilt tools).

**This is the main reason to build natively on `m93p` rather than cross-compile.**

### 4. `powershell` in an `add_custom_command`

`cmake/GenerateElxStid.ps1` is invoked via `powershell` to generate
`elxinfo.h`. Fine on Windows, another blocker for a Linux cross-build.

### 5. `strnlen_s`

MSVC secure-CRT function, absent from mingw. Used in the port's C++ layer;
needs a small shim.

### 6. Case-mismatched includes - Linux-only concern

Nine includes differ in case from the file on disk (`#include "debug.h"` vs
`Opus/DEBUG.H`, plus `pic3.c`, `pic.h`, `rareflag.h`, `rtf.h`, `rtftbl.h`,
`saveFast.h`, `screen.h`, `spell.h`), as does `DbgHelp.h` vs mingw's
`dbghelp.h`. Invisible on NTFS, fatal on ext4. Only matters if cross-compiling;
fixable with lowercase symlinks rather than by editing original sources.

## Verified NOT to be problems

Worth recording so these aren't re-investigated:

- **No SEH** anywhere in `src/` - zero `__try` / `__except` / `__finally`.
  This is usually the wall for GCC on Win32 code of this era.
- **No MSVC inline assembly** (`__asm`) - would have been unfixable under GCC.
- No MSVC intrinsics, no `<intrin.h>`, no `<crtdbg.h>`, no `#pragma warning`.
  Only 2 occurrences each of `__declspec` / `__cdecl` / `__stdcall`.
- **GCC 10's C++20 is adequate** - 35 of 40 `port/original/*.cpp` compile clean
  with `-std=c++20`. No `<ranges>`, `<format>` or concepts failures. The earlier
  worry about GCC 10 being too old for `cxx_std_20` did not materialise.
- **`msopc.h` ships with mingw-w64.** The OPC/`ComPtr` incomplete-type errors
  were purely `_WIN32_WINNT` version-gating; setting it to `0x0A00` cut the C++
  error count from 148 to 62 and fixed all SRWLock / `GetTickCount64` / OPC
  complaints. Already handled in `CMakeLists.txt`.

## Next steps, in order

1. **Install the mingw toolchain on `m93p`** (~350-450 MB, several minutes of
   fan noise - warn the user first):
   ```
   C:\tools\msys64\usr\bin\pacman.exe -S --noconfirm --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
   ```
   Installs to `C:\tools\msys64\mingw64\bin` and also solves the missing-CMake
   problem on that machine.
2. Apply fix **#1** (`disp.h`) and fix **#2** (`mkcmd.c` first - it gates the
   generated headers).
3. Configure on `m93p`: `cmake --preset x64-mingw-debug` from `C:\dev\msword\src`
   with `C:\tools\msys64\mingw64\bin` ahead on PATH. Expect new failures - the
   generated-header stage (`opuscmd.h` etc.) has never run under GCC.
4. Re-sweep on Linux after each source fix; it is free there. Harness used this
   session (case-alias symlink farm + generated `opus_windows_sdk.h`) is
   described in the git history of this file rather than kept in-tree.
5. Once `WORD1.exe` links, run the existing ctest suite on `m93p` and diff
   behaviour against an MSVC build before trusting the mingw output - the
   unsigned-char question in particular deserves a targeted test on the
   international-character routines.

## Open questions

- Should mingw be a *supported* configuration or only a convenience? If
  supported, `/guard:cf`, `/sdl`, `/CETCOMPAT` and `/HIGHENTROPYVA` have no full
  GNU equivalents, so the mingw binary is measurably less hardened. Only
  `--dynamicbase`, `--high-entropy-va` and `--nxcompat` are wired up.
- Is `C:\dev\msword` the permanent home on `m93p`, or should the machine clone
  from GitHub instead? It was copied via tar over SSH to preserve exact bytes
  (a `git clone` on Windows risks `core.autocrlf` conversion).
- If VS Build Tools 2022 gets installed on `m93p` anyway, most of this becomes
  optional - decide before investing in items 2-5.
