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
  `"Visual Studio 17 2022"` generator targeting x64. MSVC (`cl.exe`) is the
  primary supported compiler; the `x64-clang-debug` / `x64-clang-release`
  presets select the `ClangCL` toolset (VS component "C++ Clang tools for
  Windows") for the same generator. There is no MinGW or Ninja path
  configured.
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

The compiled `WORD1.exe` runs on a clean Windows 10/11 install with **no
further dependencies**:

- All directly linked libraries (`user32.dll`, `gdi32.dll`, `ole32.dll`,
  `comdlg32.dll`, `imm32.dll`, `dbghelp.dll`) are core OS components
  present on every Windows 10/11 install — no redistributable installers
  or bundled DLLs are required for these.
- The MSVC C/C++ runtime is linked **statically**: `src/CMakeLists.txt`
  sets `CMAKE_MSVC_RUNTIME_LIBRARY` to
  `MultiThreaded$<$<CONFIG:Debug>:Debug>` (`/MT` in Release, `/MTd` in
  Debug). `WORD1.exe` therefore does **not** need the Visual C++
  Redistributable (`vcruntime140.dll`, `msvcp140.dll`, …) on the target
  machine.
- The behaviour is controlled by the `MSWORD_STATIC_CRT` CMake option,
  `ON` by default and set explicitly by every preset. Configuring with
  `-DMSWORD_STATIC_CRT=OFF` switches all targets back to the dynamic CRT
  (`/MD`, `/MDd`), which then requires the [Microsoft Visual C++
  Redistributable for Visual Studio 2022
  (x64)](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)
  at runtime.
- The CRT choice must be identical across every object that gets linked
  together, so change it only through that option and rebuild from a clean
  cache — never per target. This is safe here because the project builds
  only static libraries and executables; if a DLL target is ever added,
  revisit the static-CRT decision (each module would then carry its own
  CRT state, so CRT objects such as `FILE*` or heap allocations must not
  cross the module boundary).
