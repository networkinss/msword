# Windows build machine (m93p)

Files in this folder exist for the Windows build/test machine. **That machine
has no git and never will** — this repository lives on the Linux workstation,
and the working copy at `C:\dev\msword` is synchronized by copying files (tar
over SSH), never by `git clone`/`pull`. That also side-steps `core.autocrlf`
altering original-source bytes.

## Machine

| | |
| --- | --- |
| Host | `m93p`, 192.168.0.80 — reach with `ssh user@m93p` (key auth) |
| OS | Windows 10 Pro 21H2 (19044), German locale |
| Working copy | `C:\dev\msword` |
| Toolchain | clang/mingw-w64 via MSYS2 at `C:\tools\msys64\mingw64\bin` |

`ping` never answers (firewall drops ICMP) — test reachability with SSH, not
ping. SSH lands in `cmd.exe`, session 0: GUI apps run invisibly, and the
focus/pixel-based UI tests fail there — run those from the desktop session.

## Toolchain install (one-time)

```
C:\tools\msys64\usr\bin\pacman.exe -S --noconfirm --needed ^
    mingw-w64-x86_64-clang mingw-w64-x86_64-gcc ^
    mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

No Visual Studio and no Windows SDK are required for the clang build.

## Everyday use

* Sync from Linux: `windows/sync-to-m93p.sh [files...]` (run on Linux;
  defaults to all files git sees as changed).
* Build/test on m93p: `windows\build.cmd [build|test|clean|run]`
  (from `C:\dev\msword`).

Build output lands in `out-clang-mingw\` (CMake), `build\` (tests, PDBs,
diagnostics), `bin\WORD1.exe`. Runtime crash diagnostics are written to
`build\WORD1-crash.txt` and `build\WORD1-cmdresolve.txt`.
