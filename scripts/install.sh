#!/usr/bin/env bash
# Atomic folder install. Does not alter any set/chain or start audio.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
move_host="${MOVE_HOST:-move.local}"
base="/data/UserData/schwung/modules/audio_fx"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
staging="$base/.stranger-stage-$stamp"
backup="/data/UserData/schwung/tmp/stranger-backup-$stamp"
source="$root/dist/stranger"
[[ -f "$source/stranger.so" && -f "$source/module.json" ]]
python3 "$root/tests/test_package.py"
ssh -o BatchMode=yes -o ConnectTimeout=7 "ableton@$move_host" "mkdir -p '$staging'"
scp -O -o BatchMode=yes -r "$source/"* "ableton@$move_host:$staging/"
expected="$(shasum -a 256 "$source/stranger.so" | cut -d ' ' -f 1)"
actual="$(ssh -o BatchMode=yes "ableton@$move_host" "sha256sum '$staging/stranger.so'" | cut -d ' ' -f 1)"
[[ "$expected" == "$actual" ]] || { echo 'Transfer hash mismatch' >&2; exit 1; }
ssh -o BatchMode=yes "ableton@$move_host" \
    "chmod 755 '$staging/stranger.so'; if [ -d '$base/stranger' ]; then mv '$base/stranger' '$backup'; fi; mv '$staging' '$base/stranger'"
printf 'Installed Stranger to %s/stranger (SHA-256 %s)\n' "$base" "$actual"
printf 'Add Stranger to an Audio FX slot. Existing sets and chains are unchanged.\n'
