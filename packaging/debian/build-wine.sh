#!/usr/bin/env bash
#
# Build the minimal 64-bit-only Wine that gets bundled into the .deb.
#
# See TODO.md §0 (thermal monitoring) and §1 (minimal Wine build). The build
# host is fanless, so temperature supervision is built in rather than left to
# the operator: caldun(1) samples in the background, and a guard loop pauses
# the compile if the CPU gets too hot and resumes it once it has cooled.
#
# Usage:
#   ./build-wine.sh [stage...]
#
# Stages (default: all of them, in this order):
#   fetch      download and verify the upstream source tarball
#   configure  run configure with the minimal flag set
#   build      make, under thermal supervision
#   install    make install into a staging tree, then strip
#
# Environment overrides:
#   WINE_VERSION    upstream version to build            (default 11.0)
#   WINE_SHA256     expected tarball checksum            (default: none, see below)
#   JOBS            make parallelism                     (default: nproc)
#   PAUSE_TEMP      pause the build above this CPU temp   (default 95 °C)
#   RESUME_TEMP     resume once back below this           (default 85 °C)
#   SAMPLE_SECONDS  thermal log sampling interval         (default 30)
#   GUARD_SECONDS   how often the guard checks the temp   (default 10)

set -euo pipefail

WINE_VERSION="${WINE_VERSION:-11.0}"
WINE_SHA256="${WINE_SHA256:-}"
JOBS="${JOBS:-$(nproc)}"
PAUSE_TEMP="${PAUSE_TEMP:-95}"
RESUME_TEMP="${RESUME_TEMP:-85}"
SAMPLE_SECONDS="${SAMPLE_SECONDS:-30}"
GUARD_SECONDS="${GUARD_SECONDS:-10}"

# packaging/debian/ -> repo root
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${REPO_ROOT}/packaging/wine"          # git-ignored
SRC_DIR="${WORK}/src/wine-${WINE_VERSION}"
BUILD_DIR="${WORK}/build"
STAGE_DIR="${WORK}/staging"
LOG_DIR="${WORK}/logs"
TARBALL="${WORK}/src/wine-${WINE_VERSION}.tar.xz"
WINE_URL="https://dl.winehq.org/wine/source/${WINE_VERSION%%.*}.0/wine-${WINE_VERSION}.tar.xz"

log()  { printf '\n=== %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# --- configure flags -------------------------------------------------------
#
# 64-bit only: vintageword.exe is x64, so the WoW64/32-bit half is dead weight.
# Consequence: this Wine cannot run 16- or 32-bit Windows binaries at all.
#
# The --without list covers device, media and network stacks a 1990 word
# processor cannot reach. Everything font- and X11-related is deliberately
# KEPT: freetype, fontconfig, xrender, xshm and friends are what make the
# layout match a Windows-native run, which is the whole fidelity requirement.
#
# CUPS is also kept on purpose -- Word has a Print command, and dropping
# printing to save a few MB would quietly break it. See TODO.md §1.
#
# Note: there is no --without-mono/--without-gecko in Wine 11.0 (they are
# downloadable prefix packages now, not build options) and no
# --without-osmesa/--without-ldap (support removed upstream). Wine 11.0
# rejects all four as unrecognized, so mono/gecko exclusion happens at
# prefix-creation time instead -- see TODO.md §3.
WINE_CONFIGURE_FLAGS=(
    --enable-win64
    --disable-tests
    # audio
    --without-alsa --without-oss --without-pulse --without-coreaudio
    # capture / media
    --without-gstreamer --without-v4l2 --without-gphoto --without-sane
    # 3D and compute (Word is pure GDI)
    --without-vulkan --without-opencl
    # networking, auth, smartcards, telephony
    --without-netapi --without-krb5 --without-gssapi
    --without-gnutls --without-pcap --without-pcsclite --without-capi
    # misc hardware / session plumbing
    --without-usb --without-sdl --without-wayland
)

# --- thermal supervision ---------------------------------------------------

WATCHER_PID=""
BUILD_PGID=""

# caldun 1.7+ is required: --json/--watch/--get are what this script drives.
check_caldun() {
    command -v caldun >/dev/null 2>&1 || die "caldun not found; it is required for thermal supervision"
    caldun --check >/dev/null 2>&1 || die "caldun --check found no usable sensors"

    local cpu
    cpu="$(caldun --get cpu)" || die "caldun --get cpu failed"
    printf 'Thermal supervision: %s, CPU now %s °C, pause >%s °C, resume <%s °C\n' \
        "$(caldun --version 2>/dev/null | head -1)" "$cpu" "$PAUSE_TEMP" "$RESUME_TEMP"
}

# Background JSONL log of every sensor, for the record required by TODO.md §7.
start_thermal_log() {
    mkdir -p "$LOG_DIR"
    caldun --watch "$SAMPLE_SECONDS" --json >>"${LOG_DIR}/thermal.jsonl" 2>/dev/null &
    WATCHER_PID=$!
}

stop_thermal_log() {
    if [[ -n "$WATCHER_PID" ]]; then
        kill "$WATCHER_PID" 2>/dev/null || true
    fi
    WATCHER_PID=""
}

# Compare two possibly-fractional temperatures without bc.
hotter_than() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a > b) }'; }

cleanup() {
    # BUILD_PGID is only still set if we are exiting while a build is running,
    # i.e. abnormally (Ctrl-C, SIGTERM, a die() elsewhere).
    #
    # The build runs under setsid, in its own session, so a terminal Ctrl-C
    # never reaches it: without this it would keep compiling orphaned after
    # the script exits. Resume it first -- a SIGSTOPped process ignores
    # SIGTERM until continued -- then terminate the whole group.
    if [[ -n "$BUILD_PGID" ]]; then
        echo "Interrupted: stopping the build (progress is kept; re-run the 'build' stage to resume)." >&2
        kill -CONT -- "-${BUILD_PGID}" 2>/dev/null || true
        kill -TERM -- "-${BUILD_PGID}" 2>/dev/null || true
        sleep 2
        kill -KILL -- "-${BUILD_PGID}" 2>/dev/null || true
    fi
    stop_thermal_log
}
trap cleanup EXIT INT TERM

# Run a command under thermal supervision, pausing it when the CPU is too hot.
#
# The command runs in its own process group (setsid) so SIGSTOP/SIGCONT reach
# every compiler process, not just make. Pausing rather than lowering -j is
# deliberate: make cannot change its parallelism mid-run, and on a fanless part
# a short stall costs less wall clock than sustained thermal throttling.
run_supervised() {
    local logfile="$1"; shift

    setsid "$@" >>"$logfile" 2>&1 &
    local pid=$!
    BUILD_PGID="$pid"

    local paused=0 pauses=0 peak=0 cpu
    while kill -0 "$pid" 2>/dev/null; do
        sleep "$GUARD_SECONDS"
        cpu="$(caldun --get cpu 2>/dev/null || echo 0)"
        hotter_than "$cpu" "$peak" && peak="$cpu"

        if (( paused == 0 )) && hotter_than "$cpu" "$PAUSE_TEMP"; then
            printf '  [thermal] %s °C > %s °C — pausing build\n' "$cpu" "$PAUSE_TEMP"
            kill -STOP -- "-${pid}" 2>/dev/null || true
            paused=1; pauses=$((pauses + 1))
        elif (( paused == 1 )) && ! hotter_than "$cpu" "$RESUME_TEMP"; then
            printf '  [thermal] %s °C < %s °C — resuming build\n' "$cpu" "$RESUME_TEMP"
            kill -CONT -- "-${pid}" 2>/dev/null || true
            paused=0
        fi
    done

    local rc=0
    wait "$pid" || rc=$?
    BUILD_PGID=""
    printf 'Peak CPU during this stage: %s °C (%d thermal pause(s))\n' "$peak" "$pauses"
    return $rc
}

# --- staging helpers -------------------------------------------------------

# Strip debug information from everything in the staged tree.
#
# Two formats are present and both matter: ELF (.so under lib/wine/x86_64-unix,
# plus the loader in bin/) and PE (the .dll/.exe under lib/wine/x86_64-windows,
# built by mingw-w64 and carrying DWARF). The PE half is by far the bigger win
# -- mshtml.dll alone drops 30 MB -> 4.6 MB.
#
# Selecting files by name or permission bit does not work: Wine's PE DLLs are
# mode 644 and named *.dll, so a `-name '*.so' -o -perm -u+x` filter silently
# skips the entire 775 MB PE tree. Ask file(1) what each file actually is.
#
# --strip-debug, not --strip-unneeded: it drops the debug sections without
# touching symbol tables, which is the conservative choice for PE images whose
# exports Wine's loader resolves at runtime.
strip_staged_tree() {
    local before after stripped=0
    before="$(du -sm "$STAGE_DIR" | cut -f1)"

    while IFS= read -r -d '' f; do
        case "$(file -b "$f")" in
            *ELF*|*PE32*)
                strip --strip-debug "$f" 2>/dev/null && stripped=$((stripped + 1))
                ;;
        esac
    done < <(find "$STAGE_DIR" -type f -print0)

    after="$(du -sm "$STAGE_DIR" | cut -f1)"
    printf 'Stripped %d binaries: %s MB -> %s MB\n' "$stripped" "$before" "$after"
}

# Remove build-time artifacts that `make install` places in the tree but that
# nothing needs at runtime: the Windows SDK headers (74 MB) and the import
# libraries the mingw-w64 link step consumes (499 files, 72 MB). Together
# they are a third of the staged tree.
#
# This is the safe half of trimming. The risky half -- deleting PE DLLs that
# Wine may load opportunistically -- belongs after the prefix and launcher
# work, where each deletion round can be re-tested. See TODO.md §1.
trim_build_artifacts() {
    local before after
    before="$(du -sm "$STAGE_DIR" | cut -f1)"

    find "$STAGE_DIR" -name '*.a' -delete
    rm -rf "${STAGE_DIR}/usr/local/include"

    after="$(du -sm "$STAGE_DIR" | cut -f1)"
    printf 'Trimmed build artifacts: %s MB -> %s MB\n' "$before" "$after"
}

# --- stages ----------------------------------------------------------------

stage_fetch() {
    log "Fetching Wine ${WINE_VERSION}"
    mkdir -p "${WORK}/src"

    if [[ ! -f "$TARBALL" ]]; then
        curl -fSL --output "$TARBALL" "$WINE_URL"
    else
        echo "Tarball already present, not re-downloading."
    fi

    local actual
    actual="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
    if [[ -z "$WINE_SHA256" ]]; then
        # WineHQ publishes no sha256sums.txt for the source tree, so the pin is
        # trust-on-first-use. Record this value and pass it back in from then on.
        warn "No WINE_SHA256 pinned. Downloaded tarball is:"
        warn "  ${actual}"
        warn "Record it in TODO.md §1 and re-run with WINE_SHA256=<hash> to verify."
    elif [[ "$actual" != "$WINE_SHA256" ]]; then
        die "checksum mismatch: expected ${WINE_SHA256}, got ${actual}"
    else
        echo "Checksum verified."
    fi

    [[ -d "$SRC_DIR" ]] || tar -xf "$TARBALL" -C "${WORK}/src"
}

stage_configure() {
    log "Configuring (64-bit only, minimal)"
    [[ -d "$SRC_DIR" ]] || die "source tree missing; run the fetch stage first"
    mkdir -p "$BUILD_DIR" "$LOG_DIR"

    ( cd "$BUILD_DIR" && "${SRC_DIR}/configure" "${WINE_CONFIGURE_FLAGS[@]}" ) \
        2>&1 | tee "${LOG_DIR}/configure.log"

    # configure downgrades missing optional libraries to warnings and silently
    # drops the corresponding feature. Surface them: each one is a decision.
    if grep -qE 'development files not found|wine will be built without' "${LOG_DIR}/configure.log"; then
        warn "configure reported missing optional dependencies:"
        grep -E 'development files not found|wine will be built without' "${LOG_DIR}/configure.log" >&2 || true
        warn "Triage each of these before trusting the resulting build."
    fi
}

stage_build() {
    log "Building with -j${JOBS} under thermal supervision"
    [[ -f "${BUILD_DIR}/Makefile" ]] || die "not configured; run the configure stage first"

    check_caldun
    start_thermal_log
    local started=$SECONDS

    run_supervised "${LOG_DIR}/build.log" make -C "$BUILD_DIR" -j"$JOBS" \
        || die "build failed; see ${LOG_DIR}/build.log"

    stop_thermal_log
    printf 'Build wall clock: %d min %d s\n' $(( (SECONDS - started) / 60 )) $(( (SECONDS - started) % 60 ))
    caldun --peak || true
}

stage_install() {
    log "Installing into staging tree and stripping"
    rm -rf "$STAGE_DIR"
    make -C "$BUILD_DIR" install DESTDIR="$STAGE_DIR" >>"${LOG_DIR}/install.log" 2>&1

    strip_staged_tree
    trim_build_artifacts

    printf 'Staged tree: %s\n' "$STAGE_DIR"
    du -sh "$STAGE_DIR"
}

# --- main ------------------------------------------------------------------

# Allow `source build-wine.sh` to pull in the functions without running a
# stage, so the thermal guard can be exercised on its own.
if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
    return 0
fi

stages=("$@")
[[ ${#stages[@]} -eq 0 ]] && stages=(fetch configure build install)

for stage in "${stages[@]}"; do
    case "$stage" in
        fetch)     stage_fetch ;;
        configure) stage_configure ;;
        build)     stage_build ;;
        install)   stage_install ;;
        *)         die "unknown stage '${stage}' (want: fetch configure build install)" ;;
    esac
done

log "Done: ${stages[*]}"
