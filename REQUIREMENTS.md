# Build Requirements

This document describes what is needed to compile `WORD1.exe` from this
repository, and what the resulting binary depends on at runtime.

## To compile

- **Operating system**: 64-bit Windows. `src/CMakeLists.txt` refuses to
  configure on anything else (`CMAKE_SIZEOF_VOID_P` must be 8, `WIN32` must
  be set) — this cannot be built on Linux or macOS, including via
  cross-compilation, as configured today.
- **Compiler / IDE**: Visual Studio 2022 with the **Desktop development
  with C++** workload. The CMake presets hard-code the
  `"Visual Studio 17 2022"` generator targeting x64 — MSVC (`cl.exe`) is the
  only supported compiler; there is no Clang/MinGW/Ninja path configured.
- **Windows SDK**: A Windows 10 or Windows 11 SDK component, installed
  through the Visual Studio installer. Configuration fails explicitly if
  `Windows Kits/10/Include/<version>/um/Windows.h` cannot be found.
- **CMake**: 3.25 or newer.
- **PowerShell**: used to drive the build and by a couple of custom
  CMake generation steps (`cmake/GenerateElxStid.ps1`).
- **No package manager dependencies**: there is no vcpkg/Conan/NuGet
  manifest. Every third-party dependency is a standard Win32 system
  library already present in the Windows SDK (`user32`, `gdi32`, `ole32`,
  `comdlg32`, `imm32`, `dbghelp`).

Build steps (PowerShell, from `src/`):

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Use the `x64-release` preset for an optimized build. See `README.md` for
the full target list and `CLAUDE.md` for how the build reconstructs
several original Microsoft build-time tools (MKCMD, MKDLG, BITAPP, etc.)
that are not present in the source archive.

## Runtime dependencies of the compiled binary

The assumption that the compiled `WORD1.exe` runs on Windows 10/11 with
**no further dependencies** is *mostly* but not entirely correct:

- All directly linked libraries (`user32.dll`, `gdi32.dll`, `ole32.dll`,
  `comdlg32.dll`, `imm32.dll`, `dbghelp.dll`) are core OS components
  present on every Windows 10/11 install — no redistributable installers
  or bundled DLLs are required for these.
- However, `CMakeLists.txt` does not set a static MSVC runtime
  (`CMAKE_MSVC_RUNTIME_LIBRARY` / `/MT`), so CMake's default applies: the
  binary links the **dynamic** MSVC C/C++ runtime (`/MD`). That means
  `WORD1.exe` depends on the Visual C++ Redistributable
  (`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`, etc.) matching
  the VS 2022 toolset being present on the target machine. Most Windows
  10/11 systems already have a compatible VC++ Redistributable installed
  (many other applications ship it), but it is not guaranteed to be present
  on a clean system — if it's missing, the exe will fail to start with a
  missing-DLL error until the [Microsoft Visual C++ Redistributable for
  Visual Studio 2022 (x64)](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)
  is installed.
- To get a genuinely dependency-free binary (only relying on core OS
  DLLs), the runtime would need to be statically linked
  (`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>`, i.e.
  `/MT` / `/MTd`) — this is not currently configured.
