# TODO — Debian package with bundled minimal Wine (x64 only)

Status 2026-08-16: planned, not started.

## Scope

- **Debian/Ubuntu x86-64 only** (Linux Mint target). No ARM64/Pi package —
  Pi has no GUI use case; ARM64 Wine stays a documented option, not built.
- No native Linux port of `src/Opus/`/`src/port/` — this wraps the existing
  `vintageword.exe` built normally via the existing Windows/mingw toolchain.
- No changes to `src/` or `CMakeLists.txt` — purely additive, this directory
  only. The `.exe` is consumed as an opaque input artifact.
- Goal: `.deb` installs with **zero outside dependencies** — no `apt`
  dependency on system Wine, everything needed is bundled inside the package.

## 1. Minimal Wine build

- [ ] Build Wine from source for `x86_64` Linux only (no 32-bit/WoW64).
- [ ] Exclude `wine-mono` (.NET) and `wine-gecko` (embedded IE/HTML) —
  not used by this app.
- [ ] Strip debug symbols from the resulting Wine tree.
- [ ] Confirm only the DLL implementations `vintageword.exe` actually needs
  are required: `user32`, `gdi32`, `ole32`, `comdlg32`, `imm32`, `dbghelp`.
- [ ] Record final on-disk size of the trimmed Wine tree (target: keep the
  `.deb` in the 100–200 MB range, not 500 MB+).

## 2. Private prefix + bootstrap

- [ ] Decide: pre-baked prefix shipped in the package vs. generated on
  first run via `postinst`/first-launch check. Prefer pre-baked for
  reproducibility unless size becomes an issue.
- [ ] Install original/period-correct fonts into the prefix so layout
  fidelity matches Windows-native rendering (page breaks, line wrapping).
- [ ] Bake any needed registry defaults / DLL override settings into the
  prefix at build time.

## 3. Launcher wrapper

- [ ] Shell script installed as `/usr/bin/vintageword` that sets
  `WINEPREFIX`/`WINEDLLPATH`/`WINEARCH` to the bundled paths and execs the
  bundled `wine64` — no reliance on any system-installed `wine`.
- [ ] Path mapping so Save As / file dialogs reach normal Linux paths
  (e.g. map the user's home directory into the prefix drive letters)
  sensibly rather than exposing raw `Z:\`.

## 4. `.deb` packaging

- [ ] `debian/control` — package metadata, `Architecture: amd64`, **no**
  `Depends: wine*` (bundled runtime makes this unnecessary).
- [ ] Payload layout under `/opt/vintageword/` (app binary + bundled Wine
  tree + prefix) plus `/usr/bin/vintageword` wrapper.
- [ ] `.desktop` file + icon for app-menu integration.
- [ ] `postinst`/`prerm` only if first-run prefix generation is chosen
  over pre-baked (see §2).
- [ ] Verify `dpkg -c`/`lintian` show no unexpected external dependency
  pulled in by the bundled binaries (check bundled Wine's own linkage
  against system libs — those *are* legitimate deps, e.g. libc, X11 libs).

## 5. Fidelity & functional testing

- [ ] Run the app for real under the bundled Wine on a plain Linux Mint
  x64 box; compare layout (fonts, page/line breaks) against a Windows-
  native run.
- [ ] Run `opus_word1_ui_test` automation (typing, selection, formatting,
  clipboard, Save As, PDF export) through the bundled Wine + Xvfb.
- [ ] Verify clipboard interop (Linux X11 clipboard <-> Wine's Windows
  clipboard emulation) for at least plain text and RTF.
- [ ] Verify Save As / Open file dialogs (comdlg32) behave sanely against
  real Linux paths via the wrapper's path mapping.
- [ ] Confirm `wineserver` fully exits a few seconds after app close (no
  lingering process) — measure with `ps`/`smem` before/after.

## 6. Measurement (replace estimates with real numbers)

- [ ] Record actual `.deb` file size and installed size.
- [ ] Record actual peak RSS/PSS (`smem -P wine`) while the app is running.
- [ ] Record cold-start time (package install → first window shown).

## Explicitly out of scope for this file

- ARM64/Pi package (no GUI need there — see discussion 2026-08-16).
- macOS packaging (different Wine driver `winemac.drv`, different build
  toolchain, codesigning/notarization — treat as a separate effort that
  reuses only the bundling *concept*, not these artifacts).
- Any native Linux rewrite of the GDI/USER-facing layer.
