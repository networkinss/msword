# TODO — Debian package with bundled minimal Wine (x64 only)

Status 2026-08-16 evening: **§1–§5 done, real install done, and the three
bugs the first install surfaced are fixed and verified headless (Xvfb) —
see "Findings from the first real install" below. The rebuilt package
(48 MB compressed, 365 MB installed, 19 dependencies) carries the fixed
exe (mingw cross build — swap in an MSVC/CI build when available) and a
cleaned prefix template. Next: reinstall + user re-test, then §1 trimming
and §6/§7.**

## Where we are

Done so far, all via `./build-wine.sh` in this directory:

| Stage | Result |
| --- | --- |
| `fetch` | Wine 11.0 source, checksum pinned (see §1) |
| `configure` | Clean — no missing development files |
| `build` | **8 min 26 s**, `make -j16`, exit 0, zero errors |
| `install` | Staged to `packaging/wine/staging/`: 906 MB → 430 MB stripped → **285 MB** trimmed |

### Measured, replacing the estimates

- **Build wall clock 8 min 26 s**, not the 25–50 min estimated. The machine
  never throttled meaningfully: peak CPU **59 °C** against a 95 °C pause
  threshold, **zero thermal pauses**, clock at 79% of observed max and
  touching the full 2.68 GHz. The fanless concern did not materialise for a
  build this size.
- **The NVMe is the thermal constraint, not the CPU.** It reached
  **73.85 °C** and was the only sensor `caldun` flagged `[WARN]` — against an
  81.85 °C hardware critical, so ~8 °C of headroom. A longer or more
  I/O-heavy stage could close that. This is the evidence for the open item in
  §0 about extending the pause guard beyond CPU-only.
- PE-format builtins confirmed in use (`-D__WINE_PE_BUILD` in the build log),
  because the mingw-w64 cross compiler was available — better fidelity than
  the ELF fallback.
- **Stripping is the single biggest size lever: 906 MB → 430 MB (804
  binaries).** The first version of the strip pass selected files by
  `-name '*.so' -o -perm -u+x` and therefore skipped the entire 775 MB PE
  tree — Wine's `x86_64-windows` DLLs are mode 644 and named `*.dll`. They
  are built by mingw-w64 and carry DWARF: `mshtml.dll` alone is 30 MB
  unstripped and 4.6 MB stripped. `strip_staged_tree` now dispatches on
  `file(1)` output instead of names or permission bits.

### Staged tree as it stands: 285 MB

| Path | Size | Contents |
| --- | --- | --- |
| `lib/` | 270 MB | `x86_64-windows` PE builtins + `x86_64-unix` `.so` |
| `share/` | 13 MB | fonts, `wine.inf`, nls |
| `bin/` | 2.5 MB | loader |

`include/` (74 MB of Windows SDK headers) and 499 `*.a` import libraries
(72 MB) were dropped — build-time artifacts with no runtime role. Both the
strip and the trim now run as part of the `install` stage, so a rebuild
reproduces 285 MB without manual steps.

### Picking this up next session

§3 and §4 are done and the app runs (see §3 for the self-test and the prefix
duplication finding). The order from here:

1. **§5 — build the first `.deb` and install it.** Deliberately before more
   trimming: everything currently works at paths that exist only on this
   machine, so the open risks are all about what happens when those paths
   change (symlink retargeting to `/opt/vintageword/wine/…`, a genuinely
   read-only prefix template, per-user provisioning on a real machine).
   A package that installs and launches turns those into ticks or bugs.
2. **Then resume trimming.** 285 MB staged + 93 MB per-user prefix. Closing
   the gap to the 100–200 MB target means deleting real PE DLLs (`wined3d`,
   `msxml3`, `opengl32`, `msi`, `windowscodecs`, `mshtml`…). Now testable:
   delete a round, re-run `--self-test` plus a GUI check, keep or revert.

Reusable test command (Xvfb, so it stays off the desktop):

```
WINEPREFIX=$PWD/packaging/wine/prefix WINEDLLOVERRIDES="mscoree,mshtml=" \
  xvfb-run -a packaging/wine/staging/usr/local/bin/wine 'C:\vintageword.exe' --self-test
```

Known-benign noise in every run: `err:vulkan:vulkan_init_once Wine was built
without Vulkan support` (deliberate, §1), `fixme:dwmapi:DwmSetWindowAttribute`,
and `wine: failed to start C:\windows\syswow64\rundll32.exe` during
`wineboot` (there is no syswow64 in a 64-bit-only build).

Note the build is incremental: interrupting it keeps `packaging/wine/build/`,
and re-running the `build` stage resumes rather than restarting.

Logs from the run are in `packaging/wine/logs/`: `configure.log`,
`build.log`, and `thermal.jsonl` (one `caldun --json` object per 30 s).

## Scope

- **Debian/Ubuntu x86-64 only** (Linux Mint target). No ARM64/Pi package —
  Pi has no GUI use case; ARM64 Wine stays a documented option, not built.
- No native Linux port of `src/Opus/`/`src/port/` — this wraps the existing
  `vintageword.exe` built normally via the existing Windows/mingw toolchain.
- No changes to `src/` or `CMakeLists.txt` — purely additive, this directory
  only. The `.exe` is consumed as an opaque input artifact.
- Goal: `.deb` installs with **zero outside dependencies** — no `apt`
  dependency on system Wine, everything needed is bundled inside the package.

## Decisions taken

| Question | Decision | Date |
| --- | --- | --- |
| Wine provenance | **Build minimal Wine from source** (not repacked WineHQ binaries) | 2026-08-16 |
| Wine build host | Linux workstation, Ryzen 7 4800U, **fanless — needs thermal monitoring** | 2026-08-16 |
| glibc floor | **Ubuntu 22.04 / glibc 2.35**, i.e. build natively on Mint 21.3, no container | 2026-08-16 |
| `.deb` assembly | **GitHub Actions**, consuming a prebuilt Wine tarball | 2026-08-16 |

### Why the build host matters

Wine binaries link against the build machine's glibc, and glibc is forward-
compatible only. Building on Mint 21.3 (Ubuntu 22.04 base, glibc 2.35) sets
the floor at 22.04, so the package installs on Mint 21 and everything newer.
Building on a newer distro — e.g. the Mint 22 / Ubuntu 24.04 box, glibc 2.39 —
would silently produce a package that refuses to start on Mint 21. If the
build ever moves to a newer machine, it must run inside an Ubuntu 22.04
container to preserve this floor.

Wine must be built for the architecture it runs on; cross-building from ARM
is not a practical route. This is amd64-only work.

### Resource expectations for the Wine build

Measured/estimated for a 64-bit-only build (~4,500 translation units) on the
4800U: **25–50 min** wall clock at `-j16` (a 15 W fanless part throttles under
sustained all-core load), **~1 GB RAM per parallel job** so ~16 GB at `-j16`,
**~15 GB disk** for the build tree, and **no GPU at any stage** — Wine
compiles against OpenGL/Vulkan headers, it does not touch a device. Expect 2–4
full builds while tuning configure flags.

## 0. Thermal monitoring during the build

The build host is fanless, so this is a prerequisite, not an afterthought.

Implemented in `build-wine.sh` — this section describes what it does and why.

- [x] Sample with **`caldun` 1.7+** (`/usr/bin/caldun`) — it reports exactly
  the three sensors that matter (`amdgpu` iGPU edge, `k10temp` CPU Tctl,
  `nvme` Composite) with a per-sensor OK/threshold verdict. Idle baseline
  2026-08-16: 42 °C / 49 °C / 63.9 °C, all OK. The script uses `--get cpu`
  for the guard loop, `--watch N --json` for the log, and `--peak` for the
  summary; `--check` is a startup precondition.
- [ ] Do **not** parse raw `sensors` output. This board's `nct6798` super-I/O
  reports unmapped thermistors as garbage — `CPUTIN +127.5 °C`,
  `AUXTIN0/2/3/4 +89…99 °C` — which alarm at idle and would make every build
  look like a fire. `caldun` already filters them out; that is the reason to
  use it.
- [x] Guard: **pause above 95 °C, resume below 85 °C** (`PAUSE_TEMP` /
  `RESUME_TEMP`; Tjmax for the 4800U is 105 °C). The build runs in its own
  process group via `setsid`, so `SIGSTOP`/`SIGCONT` reach every compiler
  process rather than just `make`. Pausing beats lowering `-j`, because make
  cannot change parallelism mid-run and a short stall costs less wall clock
  than sustained throttling. Verified working 2026-08-16 with forced
  thresholds: both branches fire, the workload still completes, and the EXIT
  trap guarantees nothing is left stopped.
- [ ] **Extend the pause guard to the NVMe.** Now evidenced rather than
  hypothetical: during the 2026-08-16 build the drive reached 73.85 °C and
  was the only sensor `caldun` flagged `[WARN]`, while the CPU never left
  normal range. The guard is still CPU-only. Drive critical is 81.85 °C
  (hardware-reported), so a pause threshold around 78 °C via
  `caldun --get drive` would fit.
- [x] Log `caldun --watch 30 --json` as JSONL to
  `packaging/wine/logs/thermal.jsonl` alongside the build log, so the thermal
  behaviour is a recorded number rather than an impression.
- [x] Report peak CPU and pause count per stage, and `caldun --peak` after
  the build — feeds §7.

## 1. Minimal Wine build

Driven by **`build-wine.sh`** in this directory. Stages are separately
runnable (`./build-wine.sh fetch`, `… configure`, `… build`, `… install`;
default is all four), so a configure-flag change does not force a re-download.
Thermal supervision per §0 is built into the `build` stage.

- [x] Version pinned: **Wine 11.0** (final), from
  `https://dl.winehq.org/wine/source/11.0/wine-11.0.tar.xz`, 33,172,240 bytes.
- [x] Checksum pinned (trust-on-first-use — WineHQ publishes no
  `sha256sums.txt` for the source tree). Fetched 2026-08-16:

      c07a6857933c1fc60dff5448d79f39c92481c1e9db5aa628db9d0358446e0701

  Pass it back in on later runs to get verification instead of a warning:
  `WINE_SHA256=c07a6857… ./build-wine.sh fetch`
- [x] Build dependencies installed 2026-08-16. Everything needed was already
  present except `libcups2-dev` and `libunwind-dev`. Notably the mingw-w64
  cross compiler (`x86_64-w64-mingw32-gcc`) is available, so Wine builds
  proper **PE-format** builtin DLLs rather than the lower-fidelity ELF
  fallback. Deliberately not installed: `libtiff-dev` (only feeds
  `windowscodecs` TIFF decoding), `libxslt`/`gnutls` (msxml and TLS, both
  irrelevant here).
- [x] Configure flag set chosen — see `WINE_CONFIGURE_FLAGS` in
  `build-wine.sh`: `--enable-win64` only (no 32-bit/WoW64), `--disable-tests`,
  and `--without-*` for audio, capture/media, 3D/compute, networking/auth/
  smartcards, and misc hardware. Everything font- and X11-related (freetype,
  fontconfig, xrender, xshm) is deliberately kept — that is what the layout
  fidelity depends on. **CUPS is kept**: Word has a Print command, so
  dropping printing to save a few MB would break it.
- [x] Configure run clean 2026-08-16 — **no** "development files not found"
  warnings at all. The only notes were four unrecognized options, since
  `--without-mono`/`--without-gecko` (now prefix packages, see §3) and
  `--without-osmesa`/`--without-ldap` (removed upstream) no longer exist in
  Wine 11.0; all four have been dropped from the script. Also `-lodbc not
  found`, irrelevant — no database access.
- [ ] Revisit the flag set after the first successful build: some `--without-*`
  entries may be unnecessary (already off by default) and others may still be
  missing. Trim it once, with evidence from the tree size.
  - Note the functional consequence: a 64-bit-only Wine **cannot run 16- or
    32-bit Windows binaries at all**. Fine here — `vintageword.exe` is x64 —
    but the bundled runtime is single-purpose and cannot double as a general
    Wine.
- [x] Build with thermal monitoring per §0 — **completed 2026-08-16**,
  `make -j16`, exit 0, zero errors ("Wine build complete."). See "Where we
  are" at the top of this file for the measured numbers.
- [x] `make install` into a staging prefix (`DESTDIR`), not the system —
  **completed 2026-08-16**, `packaging/wine/staging/`, 906 MB raw.
- [x] Strip debug symbols from the resulting tree — **completed 2026-08-16**,
  804 binaries, 906 MB → 430 MB. Selection is by `file(1)` type, not by name
  or permission bit; see "Where we are" for why that matters.
- [x] Drop `include/` (74 MB) and the 499 `*.a` import libraries (72 MB):
  build-time artifacts with no runtime role — **completed 2026-08-16**,
  430 MB → **285 MB**. Runs as `trim_build_artifacts` in the `install` stage.
- [ ] Confirm the DLL implementations `vintageword.exe` actually needs are
  present and working: `user32`, `gdi32`, `ole32`, `comdlg32`, `imm32`,
  `dbghelp`.
- [ ] Delete what survives configure but is provably unused; re-test after
  each deletion round rather than in one blind sweep.
- [ ] Record the final on-disk size of the trimmed Wine tree (target: keep
  the `.deb` in the 100–200 MB range, not 500 MB+).
- [ ] Publish the trimmed tree as a versioned tarball with a checksum, so CI
  consumes a fixed artifact instead of rebuilding Wine per push. Decide where
  it lives (GitHub Release asset is the obvious choice — it is too large for
  the git repo).
- [x] Script the whole sequence (`build-wine.sh`) so the build is repeatable
  and the configure flags are recorded in the repo, not in shell history.

## 2. LGPL compliance

Wine is LGPL-2.1+. Redistributing binaries carries obligations, and building
from source rather than repacking upstream makes them slightly *more* work,
not less — the binaries are now ours.

- [ ] Ship the Wine license text in the package (`/usr/share/doc/...`).
- [ ] Offer corresponding source: record the exact upstream version, the
  tarball checksum, and any patches applied (ideally: none).
- [ ] Preserve the ability to relink — dynamic linking against the bundled
  Wine libraries satisfies this; document it rather than assuming.
- [ ] Settle the separate question of what this project may claim about the
  Microsoft-derived Word source itself (Computer History Museum release).
  Same wording is needed by the Windows installer — see
  `../windows/TODO.md` §5. **Open.**

## 3. Private prefix + bootstrap

- [x] Prefix created 2026-08-16, `wineboot --init` exit 0, into
  `packaging/wine/prefix/` (git-ignored). Log: `logs/prefix-init.log`.
- [x] **`vintageword.exe` runs under this minimal Wine.** `--self-test`
  exits 0, and the GUI maps 16 windows under Xvfb. So the `--without-*`
  flag set in §1 cut nothing the app needs — the central risk of the
  minimal build is retired.
- [x] Do **not** install `wine-mono` / `wine-gecko` — done via
  `WINEDLLOVERRIDES="mscoree,mshtml="` at prefix creation. No download
  prompt appeared. (An empty `system32/gecko/plugin/` directory is created
  regardless; it is a placeholder, not an install.)

### Size discovery: the prefix duplicates the whole DLL store

`wineboot` copies Wine's PE builtins into `drive_c/windows/system32` as real
files — **587 of 587 DLLs, byte-identical to the staged tree**, 260 MB. A
naive package would therefore ship 285 MB + 292 MB = **577 MB** of which
260 MB is one set of files stored twice.

Replacing those copies with symlinks back into the shared tree takes the
prefix from **292 MB → 93 MB**, and `--self-test` still exits 0
(`logs/selftest-symlink.log`). Total 378 MB instead of 577 MB.

- [ ] Adopt the symlinked prefix. The symlink targets must be the **final
  installed** path (`/opt/vintageword/wine/…`), not `packaging/wine/staging`,
  so the rewrite has to happen at `.deb` build time with the target prefix
  known — a staging-path symlink would dangle on the user's machine.
- [ ] Decide pre-baked vs first-run given the above. Pre-baked is now
  affordable (93 MB) and is preferred for reproducibility; first-run
  generation would still cost the user the same 93 MB, just later.
- [ ] Remaining prefix bulk after symlinking: `system32` 43 MB (the `.exe`
  builtins duplicate too — `winedbg.exe` alone is 4.7 MB), `winsxs` 15 MB,
  `resources` 7.3 MB, `globalization` 3.3 MB. Symlinking the `.exe` files
  the same way is the obvious next cut.
- [ ] Install original/period-correct fonts into the prefix so layout
  fidelity matches Windows-native rendering (page breaks, line wrapping).
- [ ] Bake any needed registry defaults / DLL override settings into the
  prefix at build time.
- [x] The `GetPrivateProfileString` settings question (`Opus/initwin.c`,
  `save.c`, `elmisc.c`, …) is settled: the shipped prefix is read-only under
  `/opt`, and the launcher copies it per-user to
  `${XDG_DATA_HOME:-~/.local/share}/vintageword/prefix` on first run. The
  copy is affordable *because* of the symlinking above — 93 MB, not 292 MB.

## 4. Launcher wrapper

Implemented as `vintageword` in this directory; installs as
`/usr/bin/vintageword`. `VINTAGEWORD_ROOT` overrides the install root
(default `/opt/vintageword`) so it can be exercised from the source tree
against a mock root before any package exists.

- [x] Shell script that sets `WINEPREFIX`/`WINEARCH`/`WINELOADER` to the
  bundled paths and execs the bundled loader — no reliance on any
  system-installed Wine. POSIX `sh`, shellcheck clean.
- [x] Per-user prefix provisioning on first run, with a version stamp
  (`.vintageword-prefix-version`) so a package upgrade refreshes it. `cp -a`
  preserves the DLL symlinks, so the per-user copy stays at 88 MB.
- [x] `WINEDEBUG=-all` by default so Wine's `fixme:`/`err:` noise stays out
  of an end user's terminal; overridable for debugging.
- [x] Verified 2026-08-16: first run provisions and exits 0; second run
  reuses the prefix with completely clean output; bumping the template
  version re-provisions; a missing install root fails with a clear message
  rather than a Wine backtrace.
- [x] Path mapping: `H:` is symlinked to the user's home, so `winepath -w`
  reports `$HOME` as `H:\` and file dialogs land somewhere sensible instead
  of `Z:\home\<user>`. The launcher also `cd`s to `$HOME` and translates
  existing paths on its command line through `winepath -w`, so
  `vintageword ~/letter.doc` works. Non-path arguments pass through
  untouched, which is what keeps the app's own flags working.

## 5. `.deb` packaging

Built by `packaging/debian/build-deb.sh`. First package:
**54 MB compressed, 394 MB installed, 19 dependencies.**

- [x] `debian/control` — package metadata, `Architecture: amd64`, **no**
  `Depends: wine*` (bundled runtime makes this unnecessary). Version is
  derived from `src/CMakeLists.txt`; the script dies rather than guess.
- [x] Payload layout under `/opt/vintageword/` (app binary + bundled Wine
  tree + prefix) plus `/usr/bin/vintageword` wrapper.
- [ ] `.desktop` file + icon for app-menu integration. `.desktop` written;
  icon still to be derived from `src/port/icons/ICON8_1.ico` (convert to
  PNG at the standard hicolor sizes).
- [x] `postinst`/`prerm` — not needed. The prefix is pre-baked and copied
  per-user by the launcher on first run (§3/§4), so nothing runs as root.
- [x] Verify `dpkg -c`/`lintian` show no unexpected external dependency.
  See "Dependency discovery" below.
- [ ] Decide on lintian `dir-or-file-in-opt`. That tag enforces Debian
  *archive* policy; `/opt` is FHS-correct for locally distributed add-on
  software. Recommendation: suppress, don't restructure. User's call.

### Dependency discovery

`ldd` is the wrong tool here, and got this wrong in **both** directions:

- **Too broad.** `ldd` walks the transitive closure, so ffmpeg's own
  dependencies (x264, zmq, bluray, OpenCL, ~80 packages) landed in
  `Depends`. Debian resolves transitivity through each package's own
  `Depends`; only direct links belong in ours.
- **Too narrow on what matters.** Wine `dlopen`s its font and X11
  extension libraries — `libfreetype.so.6` and `libfontconfig.so.1` from
  `win32u.so`, `libXcursor`/`libXfixes`/`libXinerama`/`libXi`/`libXrandr`
  from `winex11.so`. None of those appear in any ELF link record.
  `libfreetype6` had only ever been present *by accident*, as a transitive
  dep of ffmpeg — so dropping `winedmo.so` (below) silently dropped the
  entire font stack, which would have shipped a package that installs
  cleanly and then can't render text.

`compute_depends()` therefore reads direct `DT_NEEDED` entries with
`objdump -p`, plus bare SONAME strings from the `x86_64-unix` `.so` files
to catch `dlopen`, resolves each through `ldconfig -p`, and maps to
packages with `dpkg -S`. Under usrmerge `ldconfig` reports
`/lib/x86_64-linux-gnu/...` while dpkg records `/usr/lib/...` and matches
on the literal string, so both spellings are tried.

The build hard-fails if `libx11-6`, `libfreetype6` or `libfontconfig1` is
missing, or if discovery finds nothing at all. That assertion has now
caught two separate discovery bugs; without it both would have shipped
silently, since a plausible-looking dependency list gives no signal that
it is incomplete.

Resulting 19: `libc6 libcups2 libdbus-1-3 libegl1 libfontconfig1
libfreetype6 libgl1 libudev1 libunwind8 libx11-6 libxcomposite1
libxcursor1 libxext6 libxfixes3 libxi6 libxinerama1 libxrandr2
libxrender1 libxxf86vm1`.

`libcups2` stays: the GUI run logged `psdrv:ext_escape QUERYESCSUPPORT`,
so the app does touch the PostScript/printing path.

### `winedmo` removed

Wine 11's media demuxer links ffmpeg and was the sole source of
`libavcodec`, `libx264`, `libzmq`, `libbluray` and `OpenCL`. Word 1.1a has
no use for it. `winedmo.so`/`winedmo.dll` moved to `packaging/wine/removed/`
and the prefix symlink deleted; self-test exits 0 and the GUI still maps
windows.

### Build-machine identity

Three separate leaks, each now guarded so a regression fails the build:

- symlinks in the prefix pointing into the build tree (587 retargeted to
  `/opt/vintageword/wine/...`);
- registry font paths (`.reg` stores *doubled* backslashes, which is why
  the first `sed` attempt silently did nothing — now `perl` with a literal
  match);
- the build user's account name in the profile directory and ~84 registry
  entries, plus 7 profile symlinks into `$HOME` including this machine's
  locale-specific `Desktop -> /home/amrit/Schreibtisch`.

- [ ] **Real install test** — everything so far was verified against mock
  roots. `sudo apt install packaging/dist/vintageword_0.2.0_amd64.deb`,
  then launch.
- [ ] CI job assembling the `.deb` from (a) the `vintageword.exe` artifact
  the existing `Build` workflow uploads and (b) the pinned Wine tarball from
  §1. Runner must be `ubuntu-22.04` to match the glibc floor.

## Findings from the first real install (2026-08-16)

The package installs and the app runs. First real use surfaced three bugs;
all three were root-caused the same day. Reproduction and debugging were
done headless with Xvfb + an XTEST keystroke injector
(`packaging/debian/tools/xkeys.c`), screenshots via ImageMagick `import`,
and `WINEDEBUG=+font,+psdrv`.

### 1. Document text unreadably small (app bug, printer-dependent)

With a CUPS printer present, document text at nominal 10pt renders ~4px;
with `CUPS_SERVER` pointed at a dead port it renders correctly. Chain:

- Word 1.x formats against the printer and picks screen fonts to match the
  printer font's *actual* size (`LOADFONT.C`, `fcidActual.hps` store-back).
- Default-size text reaches `FGraphicsFcidToPlf` with `hps == 0` — in
  violation of the function's own `Assert(fcid.hps > 0)` — which becomes a
  `lfHeight = 0` request: "give me the device default font".
- Windows resolves height 0 against the device; **Wine's font mapper
  resolves it to a fixed 16px regardless of the DC's dpi**. On psdrv's
  300dpi DC, 16 dots read back as ~3.5pt (hps 7), and the screen font is
  then created at 3.5pt → 5px.
- Verified live via the app's own test hooks (`WM_APP+0x351`):
  `hpsFcidMax = 7`, `dxsInch = 96`, `dxuInch = 300`.

Fix: `LOADFONT.C` `FGraphicsFcidToPlf` OPUS_X64 island resolves
`hps == 0` to `hpsDefault` (20 = 10pt — the size the ribbon already
displays), honouring the original contract on both platforms.

### 2. `D0000001.DOC` shown in status bar and save prompt (app bug)

The port's long-filename adapter (opus_sdm_runtime.cpp) has Word work on
an 8.3-safe staging file (`...\W<hex>\D0000001.DOC`) and copies to the
user's chosen path afterwards; displays are supposed to be overlaid with
the chosen name via `OpusWin95DisplayAlias`. That overlay was only wired
into window titles (`wwchange.c`), so the status line and the "Save
changes to ..." prompt — both built by `prompt.c` `BuildStMstRgw` case
`\006` — showed the staging name. Not Wine-specific; the same happens on
Windows. Fix: apply the alias in case `\006` (full path looked up first,
since the alias map is keyed by full path). Two follow-ups in
`OpusWin95DisplayAlias` itself: the "N Chars." prompt fires *before*
`OpusFinishWin95SaveAlias` registers the mapping, so the display alias now
also consults the still-active save alias; and a leaf-match fallback
covers normalizer differences in the path form (staging leaves are unique
per session). Verified: status bar and save-changes prompt both show the
chosen name.

The user also reported the app refusing to close from that prompt; the
keyboard path (Alt+Y) closes cleanly in repro, so this may have been the
save-alias copy failing or a mouse-path issue — retest after the fixes.

### 3. m93p state shipped in the prefix template (packaging bug)

`bin/WINWORD.INI` — Word's binary pref file written next to the exe by
test runs on m93p — was read during prefix creation and persisted as
`C:\WINWORD.INI` in the template: cached printer "Microsoft Print to PDF"
at 600dpi, a font cache computed against it, and
`C:\USERS\USER\...\D0000001.DOC` MRU entries. `build-deb.sh` now deletes
WINWORD.INI, stray `*.doc`, Temp leftovers, and the stale 18 MB
`drive_c\vintageword.exe` copy from the template, and fails the build if
a WINWORD.INI survives. (Removing the INI did not fix bug 1 — that
reproduces on a clean prefix — but machine state must not ship.) Package
after cleanup: 48 MB compressed, 365 MB installed.

### Local rebuild path (no Windows box needed)

The fixes were rebuilt and verified on this machine via a mingw cross
build: `cmake -B out-cross -DCMAKE_SYSTEM_NAME=Windows` with
`x86_64-w64-mingw32-gcc`, the bundled Wine running the build-time codegen
tools (wrapper scripts convert Unix paths to Windows form — the DOS-era
tools parse `/home/...` as switches), a `powershell` shim backed by a
Python port of `GenerateElxStid.ps1`, and lowercase symlinks for
case-sensitive `#include`s. A handful of gcc-strictness fixes landed in
original sources (forward declarations for static functions, GNU spelling
of two flexible array members). MSVC on m93p remains the primary
toolchain; re-verify there when the box is on.

## 6. Fidelity & functional testing

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

## 7. Measurement (replace estimates with real numbers)

- [ ] Record actual `.deb` file size and installed size.
- [ ] Record actual peak RSS/PSS (`smem -P wine`) while the app is running.
- [ ] Record cold-start time (package install → first window shown).
- [ ] Record the Wine build's actual wall-clock time and peak Tctl, to
  replace the estimates in "Resource expectations" above.

## Explicitly out of scope for this file

- ARM64/Pi package (no GUI need there — see discussion 2026-08-16).
- macOS packaging (different Wine driver `winemac.drv`, different build
  toolchain, codesigning/notarization — treat as a separate effort that
  reuses only the bundling *concept*, not these artifacts).
- Any native Linux rewrite of the GDI/USER-facing layer.
