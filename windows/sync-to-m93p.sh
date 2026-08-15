#!/usr/bin/env bash
# Sync repository files to the git-less Windows build machine (m93p).
# Run from anywhere inside the repo on the Linux workstation.
#
#   windows/sync-to-m93p.sh              # everything git sees as changed/untracked
#   windows/sync-to-m93p.sh src/...      # explicit paths only
#
# Copies as a tar stream over SSH so original-source bytes are preserved
# exactly (no CRLF translation, which is also why the machine has no git).
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

if [ "$#" -gt 0 ]; then
    files=("$@")
else
    mapfile -t files < <(git status --porcelain | sed -E 's/^.{3}//; s/^"(.*)"$/\1/')
fi

if [ "${#files[@]}" -eq 0 ]; then
    echo "Nothing to sync."
    exit 0
fi

printf 'Syncing %d file(s) to user@m93p:C:/dev/msword\n' "${#files[@]}"
tar -cf - "${files[@]}" | ssh user@m93p 'cd /d C:\dev\msword & tar -xf -'
echo "Done."
