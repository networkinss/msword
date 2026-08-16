# TODO — Debian package with bundled minimal Wine (x64 only)

Status 2026-08-16: **§1 Wine build done. Next: the `install` stage (§1) —
`./build-wine.sh install`.**

## Where we are

Done so far, all via `./build-wine.sh` in this directory:

| Stage | Result |
| --- | --- |
| `fetch` | Wine 11.0 source, checksum pinned (see §1) |
| `configure` | Clean — no missing development files |
| `build` | **8 min 26 s**, `make -j16`, exit 0, zero errors |
| `install` | **not run yet** — next step |

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

### Picking this up next session

1. `./build-wine.sh install` — stages into `packaging/wine/staging/` via
   `DESTDIR`, strips, and prints `du -sh`. That gives the first real number
   against the 100–200 MB target in §1.
2. Then §1's trimming and size recording, and §3 (private prefix).

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
- [ ] `make install` into a staging prefix (`DESTDIR`), not the system.
- [ ] Strip debug symbols from the resulting tree.
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

- [ ] Decide: pre-baked prefix shipped in the package vs. generated on
  first run via `postinst`/first-launch check. Prefer pre-baked for
  reproducibility unless size becomes an issue.
- [ ] Do **not** install `wine-mono` (.NET) or `wine-gecko` (embedded
  IE/HTML) into the prefix — neither is used by this app, and in Wine 11.0
  excluding them is purely a prefix-creation decision (there is no configure
  option). Create the prefix with `WINEDLLOVERRIDES="mscoree,mshtml="` so
  Wine never prompts to download them.
- [ ] Install original/period-correct fonts into the prefix so layout
  fidelity matches Windows-native rendering (page breaks, line wrapping).
- [ ] Bake any needed registry defaults / DLL override settings into the
  prefix at build time.
- [ ] Note: the original source uses `GetPrivateProfileString`-style settings
  (`Opus/initwin.c`, `save.c`, `elmisc.c`, …). Confirm where those land in
  the prefix and that they persist per-user rather than in a read-only
  `/opt` prefix — this likely forces a per-user copy of at least part of the
  prefix on first run.

## 4. Launcher wrapper

- [ ] Shell script installed as `/usr/bin/vintageword` that sets
  `WINEPREFIX`/`WINEDLLPATH`/`WINEARCH` to the bundled paths and execs the
  bundled `wine64` — no reliance on any system-installed `wine`.
- [ ] Path mapping so Save As / file dialogs reach normal Linux paths
  (e.g. map the user's home directory into the prefix drive letters)
  sensibly rather than exposing raw `Z:\`.

## 5. `.deb` packaging

- [ ] `debian/control` — package metadata, `Architecture: amd64`, **no**
  `Depends: wine*` (bundled runtime makes this unnecessary).
- [ ] Payload layout under `/opt/vintageword/` (app binary + bundled Wine
  tree + prefix) plus `/usr/bin/vintageword` wrapper.
- [ ] `.desktop` file + icon for app-menu integration. Icon can be derived
  from `src/port/icons/ICON8_1.ico` (convert to PNG at the standard hicolor
  sizes).
- [ ] `postinst`/`prerm` only if first-run prefix generation is chosen
  over pre-baked (see §3).
- [ ] Verify `dpkg -c`/`lintian` show no unexpected external dependency
  pulled in by the bundled binaries (check bundled Wine's own linkage
  against system libs — those *are* legitimate deps, e.g. libc, X11 libs).
- [ ] CI job assembling the `.deb` from (a) the `vintageword.exe` artifact
  the existing `Build` workflow uploads and (b) the pinned Wine tarball from
  §1. Runner must be `ubuntu-22.04` to match the glibc floor.

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
