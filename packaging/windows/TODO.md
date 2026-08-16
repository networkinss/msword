# TODO — Windows installer (Inno Setup)

Status 2026-08-16: planned, not started.

## Decisions taken

| Question | Decision | Date |
| --- | --- | --- |
| Installer technology | **Inno Setup** (`.iss` script) | 2026-08-16 |
| `.doc` file association | **Optional, unchecked by default** | 2026-08-16 |
| Build location | **GitHub Actions** (Windows runner) | 2026-08-16 |

Inno Setup was chosen over WiX/NSIS/CPack because the payload is a single
self-contained `.exe`: there is nothing to merge-module, no runtime to chain,
and no reason to touch `src/CMakeLists.txt`. It is also preinstalled on the
GitHub `windows-*` runner images, so CI needs no extra install step.

## Scope

- **x64 only for the first installer.** The ARM64 binary that
  `.github/workflows/build.yml` already produces is a candidate for a second
  package later, but ARM64 Windows runs x64 binaries under emulation anyway,
  so the x64 installer covers every Windows target on day one.
- Payload is `bin/vintageword.exe` and nothing else — no redistributable, no
  data files (see `../README.md`).
- Release configuration only. Debug builds are not shipped.

## 1. Installer script

- [ ] `packaging/windows/vintageword.iss`.
- [ ] `AppId` — generate one GUID **once** and never change it; it is what
  lets a later version upgrade in place instead of installing alongside.
- [ ] Version taken from the built `.exe`'s `VS_VERSION_INFO` (Inno's
  `GetVersionNumbersString`) or passed in via `/DAppVersion=` from the CI
  step. Must not be hardcoded — fail the build if unset.
- [ ] `PrivilegesRequiredOverridesAllowed=dialog` so the user can choose a
  per-user install (`%LOCALAPPDATA%\Programs\VintageWord`, no admin) or a
  per-machine install (`%ProgramFiles%\VintageWord`, admin). Per-user is the
  friendlier default for a hobby/retro app.
- [ ] `ArchitecturesAllowed=x64compatible`,
  `ArchitecturesInstallIn64BitMode=x64compatible`.
- [ ] Start Menu shortcut; desktop shortcut as an *optional* task, unchecked.
- [ ] Uninstaller entry with the icon from `src/port/icons/ICON8_1.ico`.
- [ ] `LICENSE`/about text — must state the historical-source provenance
  (Microsoft Word for Windows 1.1a source, Computer History Museum release)
  rather than implying this is a Microsoft product. Coordinate the wording
  with `README.md` at the repo root; see §5.

## 2. File association (optional task)

- [ ] `[Tasks]` entry `associatedoc`, `Flags: unchecked`.
- [ ] Registry entries written only when that task is selected, and only
  under the hive matching the install scope (`HKA` resolves to `HKCU` for a
  per-user install, `HKLM` for per-machine — do not hardcode `HKCR`).
- [ ] Use a `VintageWord.Document` ProgID; never overwrite the existing
  `.doc` default verb destructively — write the ProgID and point `.doc` at
  it, so uninstall can restore cleanly.
- [ ] `[UninstallDelete]`/registry cleanup must remove the ProgID and, if
  and only if `.doc` still points at it, unset that.
- [ ] Manually verify on a machine that has a real Word or LibreOffice
  installed that declining the task leaves associations untouched.

## 3. CI job

- [ ] New job in `.github/workflows/` (or a `release.yml`) that:
  - [ ] builds the release preset (`x64-clang-mingw-release`, and/or
    `x64-release` once the MSVC path is green — it is still
    `continue-on-error` today),
  - [ ] runs the headless ctest suite as a gate before packaging,
  - [ ] runs `iscc` on `vintageword.iss`,
  - [ ] uploads `VintageWord-<version>-x64-setup.exe` as an artifact.
- [ ] Decide trigger: every push (artifact only) vs. tag push (attach to a
  GitHub Release). Tag-triggered releases are the eventual goal; artifact on
  push is useful immediately.
- [ ] No code signing initially — the installer will show a SmartScreen
  warning. Document that in the README rather than pretending otherwise; a
  certificate is a separate decision with real cost.

## 4. Verification

- [ ] Install per-user, launch from the Start Menu, type and save a document,
  uninstall — confirm no leftovers in `%LOCALAPPDATA%` or the registry.
- [ ] Repeat per-machine (admin).
- [ ] Install over an existing older version and confirm in-place upgrade
  (same `AppId`) rather than a duplicate entry in Apps & Features.
- [ ] Confirm the installed `.exe` runs on a clean Windows 10/11 image with
  **no** Visual C++ Redistributable present — this is the whole point of the
  static CRT and is worth testing rather than assuming.
- [ ] Verify on m93p (Windows 10 19044, German locale) that the installer UI
  and paths behave under a non-English locale.

## 5. Open questions

- [ ] Exact licensing/attribution wording for the installer's about/license
  page. The upstream source is a Computer History Museum release of Microsoft
  code; what this project may claim and must disclaim needs to be settled
  once and reused in the `.deb`, the README, and here.
- [ ] Ship a portable `.zip` alongside the installer? Cheap to add (the
  payload is one file) and welcome for users who dislike installers.
- [ ] Whether to publish the MSVC or the clang/mingw build as *the* release
  binary. clang/mingw is the configuration the project is developed and
  verified against; MSVC CI is still informational.
