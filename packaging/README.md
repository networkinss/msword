# Packaging

Installers for `vintageword.exe`. Two targets, one shared input.

| Directory | Produces | Technology |
| --- | --- | --- |
| `windows/` | `VintageWord-<version>-x64-setup.exe` | Inno Setup |
| `debian/` | `vintageword_<version>_amd64.deb` | bundled minimal Wine + `dpkg-deb` |

## The shared input

Both installers consume `bin/vintageword.exe` as an **opaque artifact**. Nothing
under `packaging/` may require changes to `src/` or `src/CMakeLists.txt`.

That works because the executable is genuinely self-contained: every target in
the project is a static library or an executable (no DLLs), `MSWORD_STATIC_CRT`
is `ON` by default for MSVC (`/MT`) and the mingw configuration adds
`-static -static-libgcc -static-libstdc++`. The binary imports only core
Windows DLLs — `user32`, `gdi32`, `ole32`, `comdlg32`, `imm32`, `dbghelp` — and
ships no external runtime data files. So "install" means "place one `.exe`",
which is what keeps both packages simple.

## Staging the Windows-built executable on Linux

The `.deb` is assembled entirely on Linux, but its payload is a Windows
binary. `vintageword.exe` is therefore built on Windows and copied into
`bin/` here — the same path a local CMake build would write it to, and
already git-ignored, as are `packaging/wine/` (the bundled Wine tree) and
`packaging/dist/` (built packages).

Packaging scripts take the executable path as a **parameter**, defaulting to
`bin/vintageword.exe`. The same script then serves both the local loop
(hand-staged binary) and the CI job (which downloads the
`vintageword-clang-x64` artifact and passes its path), instead of CI needing
a separate code path.

Once staged, no further Windows access is needed for packaging work — the
Wine build, `dpkg-deb`, prefix staging, `lintian`, and the Xvfb UI tests are
all Linux-native. Windows is needed again only for a fidelity comparison
(§6 of `debian/TODO.md`) or when the source changes.

## Versioning

Single source of truth is `project(MicrosoftWordX64Port VERSION x.y.z)` in
`src/CMakeLists.txt`, mirrored by hand in `src/port/word1.rc`
(`FILEVERSION`/`PRODUCTVERSION`/the `StringFileInfo` strings). Currently
**0.2.0**.

Packaging scripts must *derive* the version from the built binary or from
`src/CMakeLists.txt` rather than hardcoding it, so a version bump does not
silently produce a mislabelled installer. A packaging step that cannot
determine the version must fail rather than guess.

## Where things are built

Both installers are produced by GitHub Actions (`.github/workflows/`), on top
of the executables the existing `Build` workflow already uploads
(`vintageword-clang-x64`, `vintageword-msvc-*`, `vintageword-clang-arm64`).

The one exception is the bundled Wine tree for the `.deb`: it is compiled from
source on the Linux workstation (see `debian/TODO.md` §1), published as a
versioned tarball, and consumed by CI. It is far too slow to rebuild on every
push, and its configure flags need an interactive tuning loop.

## Status

- **Debian** — in progress. Minimal Wine 11.0 built from source, staged and
  trimmed to 285 MB (2026-08-16); next steps are the private prefix and the
  launcher wrapper. See `debian/TODO.md`, which opens with a "Where we are"
  section.
- **Windows** — planned, not started. See `windows/TODO.md`.

The payload `bin/vintageword.exe` is staged (copied from m93p and renamed from
`WORD1.exe`); it is a debug build, adequate for developing packaging mechanics.
