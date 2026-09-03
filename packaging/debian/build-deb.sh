#!/usr/bin/env bash
#
# Assemble the vintageword .deb from the bundled Wine tree, the prefix
# template and a Windows-built vintageword.exe.
#
# Inputs (all produced elsewhere):
#   packaging/wine/staging/usr/local   the trimmed Wine tree  (build-wine.sh)
#   packaging/wine/prefix              the symlinked prefix    (see TODO.md §3)
#   bin/vintageword.exe                the payload, built on Windows
#
# Output:
#   packaging/dist/vintageword_<version>_amd64.deb
#
# Usage:
#   ./build-deb.sh [path/to/vintageword.exe]
#
# See TODO.md §5.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGE_DIR="${REPO_ROOT}/packaging/wine/staging/usr/local"
PREFIX_SRC="${REPO_ROOT}/packaging/wine/prefix"
DIST="${REPO_ROOT}/packaging/dist"
EXE_SRC="${1:-${REPO_ROOT}/bin/vintageword.exe}"

# Where the package installs. Baked into symlinks and the registry, so it
# cannot be changed after the fact without rebuilding.
INSTALL_ROOT="/opt/vintageword"

# Neutral profile name in the shipped prefix. The launcher renames it to the
# real user on first run; it must not be a name that occurs incidentally in
# the registry, so keep it distinctive.
TEMPLATE_USER="vintageword"

log() { printf '\n=== %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# --- version ---------------------------------------------------------------

# Single source of truth is src/CMakeLists.txt (see packaging/README.md).
# A packaging step that cannot determine the version must fail, not guess.
detect_version() {
    local v
    v="$(sed -n 's/^project(MicrosoftWordX64Port VERSION \([0-9.]*\).*/\1/p' \
         "${REPO_ROOT}/src/CMakeLists.txt" | head -1)"
    [[ -n "$v" ]] || die "could not read the version from src/CMakeLists.txt"
    printf '%s' "$v"
}

# --- dependency discovery --------------------------------------------------

# Wine links against system libraries (X11, freetype, fontconfig, ...). Ask
# the system which packages own them rather than hand-maintaining a list that
# silently rots: a missing entry here is an install that works on this machine
# and fails on the user's.
compute_depends() {
    local sonames soname path owner pkgs

    # Two sources, because neither alone is right:
    #
    # 1. Direct DT_NEEDED entries. Deliberately NOT `ldd`, which reports the
    #    whole transitive closure -- that pulled in ~80 packages that were
    #    really ffmpeg's own dependencies. Debian resolves transitivity through
    #    each package's own Depends, so only direct links belong here.
    #
    # 2. SONAMEs Wine dlopen()s at runtime. These carry no ELF link record at
    #    all, so no amount of link inspection finds them -- yet they include
    #    libfreetype and libfontconfig, i.e. exactly what text rendering needs.
    #    They appear as bare SONAME strings in the unix-side .so files.
    sonames="$(
        {
            find "$STAGE_DIR" -type f \( -name '*.so' -o -path '*/bin/*' \) -print0 \
                | xargs -0 -r -n 50 objdump -p 2>/dev/null \
                | awk '/NEEDED/{print $2}'

            find "${STAGE_DIR}/lib/wine/x86_64-unix" -name '*.so' -print0 \
                | xargs -0 -r -n 50 strings -a 2>/dev/null \
                | grep -E '^lib[A-Za-z0-9_+.-]+\.so\.[0-9]+$'
        } | sort -u
    )" || true

    # Resolve each SONAME to a file, then to its owning package. `dpkg -S`
    # matches on the literal path string, and on a usrmerged system ldconfig
    # reports /lib/... while dpkg recorded /usr/lib/... -- so try both.
    pkgs="$(
        while IFS= read -r soname; do
            [[ -n "$soname" ]] || continue
            path="$(ldconfig -p 2>/dev/null | awk -v s="$soname" '$1 == s {print $NF; exit}')"
            [[ -n "$path" ]] || continue
            owner="$(dpkg -S "$path" 2>/dev/null || dpkg -S "/usr${path}" 2>/dev/null || true)"
            [[ -n "$owner" ]] && printf '%s\n' "${owner%%:*}"
        done <<< "$sonames" | sort -u | grep -v '^$'
    )" || true

    [[ -n "$pkgs" ]] || die "dependency discovery found nothing -- refusing to ship a package with no Depends"

    # Wine cannot start without X11 and cannot render text without freetype.
    # If discovery did not find them, it is broken again -- fail rather than
    # ship a package whose Depends look plausible but are incomplete.
    local required="libx11-6 libfreetype6 libfontconfig1"
    local missing=""
    for p in $required; do
        grep -qx "$p" <<< "$pkgs" || missing="${missing} ${p}"
    done
    [[ -z "$missing" ]] || die "dependency discovery missed:${missing} -- discovery is broken, not the build"

    printf '%s\n' "$pkgs" | paste -sd, - | sed 's/,/, /g'
}

# --- assembly --------------------------------------------------------------

VERSION="$(detect_version)"
PKGDIR="${DIST}/vintageword_${VERSION}_amd64"

[[ -f "$EXE_SRC" ]]     || die "payload not found: ${EXE_SRC}"
[[ -d "$STAGE_DIR" ]]   || die "Wine tree not staged; run build-wine.sh first"
[[ -d "$PREFIX_SRC" ]]  || die "prefix not found: ${PREFIX_SRC}"

log "Building vintageword ${VERSION}"
rm -rf "$PKGDIR"
mkdir -p "${PKGDIR}${INSTALL_ROOT}" "${PKGDIR}/usr/bin" \
         "${PKGDIR}/usr/share/applications" \
         "${PKGDIR}/usr/share/doc/vintageword" \
         "${PKGDIR}/DEBIAN"

log "Copying the Wine tree"
cp -a "$STAGE_DIR" "${PKGDIR}${INSTALL_ROOT}/wine"
cp -a "$EXE_SRC" "${PKGDIR}${INSTALL_ROOT}/vintageword.exe"

log "Copying the prefix template"
cp -a "$PREFIX_SRC" "${PKGDIR}${INSTALL_ROOT}/prefix-template"
TEMPLATE="${PKGDIR}${INSTALL_ROOT}/prefix-template"

# --- retarget everything that names a build-machine path -------------------

# 1. The 587 system32 DLL symlinks point into packaging/wine/staging. Left
#    alone they would dangle on every user's machine -- the package would
#    install cleanly and then fail to start.
log "Retargeting DLL symlinks to ${INSTALL_ROOT}"
retargeted=0
while IFS= read -r -d '' link; do
    target="$(readlink "$link")"
    case "$target" in
        "${STAGE_DIR}"/*)
            ln -sfn "${INSTALL_ROOT}/wine/${target#"${STAGE_DIR}"/}" "$link"
            retargeted=$((retargeted + 1))
            ;;
    esac
done < <(find "$TEMPLATE" -type l -print0)
printf 'Retargeted %d symlinks\n' "$retargeted"

# 2. The font registry entries record absolute Z: paths into the build tree.
#    Fonts drive layout fidelity (page breaks, line wrapping), so a dangling
#    font path is not cosmetic -- it changes how documents render.
#    Registry files store Windows paths with doubled backslashes, so the
#    literal text is `Z:\\home\\...`. Matching that with sed means counting
#    backslashes through two levels of quoting; perl's quotemeta does it
#    correctly with no escaping by hand.
log "Rewriting registry paths"
export REG_FROM="Z:${STAGE_DIR//\//\\\\}"
export REG_TO="Z:${INSTALL_ROOT//\//\\\\}\\\\wine"
for reg in "${TEMPLATE}"/*.reg; do
    [[ -f "$reg" ]] || continue
    perl -pi -e 's/\Q$ENV{REG_FROM}\E/$ENV{REG_TO}/g' "$reg"
done

# 3. The prefix was created by whoever ran wineboot, so the profile directory
#    and ~84 registry entries carry that account name. Normalise to a neutral
#    name here; the launcher substitutes the real user at first run.
log "Normalising the profile name to '${TEMPLATE_USER}'"
build_user="$(find "${TEMPLATE}/drive_c/users" -maxdepth 1 -mindepth 1 -type d \
              ! -name Public ! -name "$TEMPLATE_USER" -printf '%f\n' | head -1 || true)"
if [[ -n "$build_user" ]]; then
    mv "${TEMPLATE}/drive_c/users/${build_user}" "${TEMPLATE}/drive_c/users/${TEMPLATE_USER}"
    for reg in "${TEMPLATE}"/*.reg; do
        [[ -f "$reg" ]] || continue
        sed -i "s|C:\\\\\\\\users\\\\\\\\${build_user}|C:\\\\\\\\users\\\\\\\\${TEMPLATE_USER}|g" "$reg"
    done
fi

# H: is created per-user by the launcher; a build-machine home would be wrong.
rm -f "${TEMPLATE}/dosdevices/h:"

# Word writes its binary pref file (C:\WINWORD.INI) on exit: cached printer
# metrics, a font cache computed against that printer, and MRU/staging file
# paths -- all state of whatever machine ran the app during prefix creation.
# Word regenerates it on first run, so ship none of it. Same for any document
# or scratch files a test run left behind.
log "Removing app state left by the prefix-creation run"
rm -f "${TEMPLATE}/drive_c/WINWORD.INI"
# The prefix-creation run also left a copy of the exe at C:\ -- the packaged
# app runs from ${INSTALL_ROOT}/vintageword.exe, so this is 18 MB of dead
# weight that would silently go stale.
rm -f "${TEMPLATE}/drive_c/vintageword.exe"
find "${TEMPLATE}/drive_c/users" -iname '*.doc' -delete
find "${TEMPLATE}/drive_c/users" -ipath '*/Temp/*' -type f -delete
find "${TEMPLATE}/drive_c/users" -ipath '*/Temp/*' -depth -type d -empty -delete
if find "$TEMPLATE" -iname 'WINWORD.INI' | grep -q .; then
    die "a WINWORD.INI survived template cleanup"
fi

# 4. wineboot points the profile's Desktop/Documents/Downloads at the build
#    user's XDG directories -- including their locale-specific names, e.g.
#    Desktop -> /home/<builder>/Schreibtisch. Those are absolute paths to an
#    account that does not exist on the target machine. Replace them with real
#    directories inside the prefix; the launcher maps the user's actual home
#    to H: separately.
log "Replacing build-user profile symlinks with real directories"
profile_fixed=0
while IFS= read -r -d '' link; do
    case "$(readlink "$link")" in
        /*)
            rm -f "$link"
            mkdir -p "$link"
            profile_fixed=$((profile_fixed + 1))
            ;;
    esac
done < <(find "${TEMPLATE}/drive_c/users" -type l -print0)
printf 'Replaced %d profile symlinks\n' "$profile_fixed"

printf '%s\n' "$VERSION" > "${TEMPLATE}/.vintageword-prefix-version"
printf '%s\n' "$TEMPLATE_USER" > "${TEMPLATE}/.vintageword-template-user"

# Fail loudly rather than shipping a prefix that still names this machine.
if grep -rqs 'IdeaProjects' "${TEMPLATE}"/*.reg; then
    die "build paths still present in the prefix registry after rewriting"
fi
if find "$TEMPLATE" -type l -lname "${STAGE_DIR}/*" | grep -q .; then
    die "symlinks into the build tree survived retargeting"
fi
# Nothing anywhere in the package may point at this machine's home directory.
if find "$PKGDIR" -type l -lname "${HOME}/*" -o -type l -lname "${HOME}" | grep -q .; then
    die "symlinks into the build user's home survived rewriting"
fi

# --- metadata --------------------------------------------------------------

log "Writing package metadata"
cp "${REPO_ROOT}/packaging/debian/vintageword" "${PKGDIR}/usr/bin/vintageword"
chmod 755 "${PKGDIR}/usr/bin/vintageword"

DEPENDS="$(compute_depends)"
INSTALLED_KB="$(du -sk "$PKGDIR" | cut -f1)"

cat > "${PKGDIR}/DEBIAN/control" <<EOF
Package: vintageword
Version: ${VERSION}
Section: editors
Priority: optional
Architecture: amd64
Maintainer: VintageWord packaging <oliver.glas@inss.ch>
Depends: ${DEPENDS}
Installed-Size: ${INSTALLED_KB}
Description: Microsoft Word for Windows 1.1a, ported to x64 and bundled with Wine
 VintageWord is a native x64 port of the original Word for Windows 1.1a
 source released by the Computer History Museum.
 .
 This package bundles a minimal, 64-bit-only build of Wine to run it, so it
 has no dependency on any system Wine installation and does not interfere
 with one. The bundled Wine is single-purpose: it cannot run 16- or 32-bit
 Windows binaries.
EOF

cat > "${PKGDIR}/usr/share/applications/vintageword.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=VintageWord
Comment=Microsoft Word for Windows 1.1a
Exec=vintageword %f
Terminal=false
Categories=Office;WordProcessor;
MimeType=application/msword;
EOF

# LGPL obligations for the bundled Wine -- see TODO.md §2.
cp "${STAGE_DIR}/share/doc/wine/COPYING.LIB" \
   "${PKGDIR}/usr/share/doc/vintageword/COPYING.LIB.wine" 2>/dev/null \
   || cp "${REPO_ROOT}/packaging/wine/src/wine-"*/COPYING.LIB \
         "${PKGDIR}/usr/share/doc/vintageword/COPYING.LIB.wine"

# --- build -----------------------------------------------------------------

log "Building the .deb"
find "$PKGDIR" -type d -exec chmod 755 {} +
chmod 755 "${PKGDIR}/usr/bin/vintageword"

DEB="${DIST}/vintageword_${VERSION}_amd64.deb"
dpkg-deb --root-owner-group --build "$PKGDIR" "$DEB"

log "Done"
ls -lh "$DEB"
printf 'Installed size: %s MB\n' "$((INSTALLED_KB / 1024))"
printf '\nInstall with:\n  sudo apt install %s\n' "$DEB"
