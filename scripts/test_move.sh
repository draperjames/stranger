#!/usr/bin/env bash
# Offline native loader, functional/safety sweeps and timing. Never opens audio.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
move_host="${MOVE_HOST:-move.local}"
remote="/data/UserData/schwung/tmp/stranger-test"
ssh -o BatchMode=yes -o ConnectTimeout=7 "ableton@$move_host" "mkdir -p '$remote'"
scp -O -o BatchMode=yes "$root/build/arm64/test_stranger" "$root/build/arm64/stranger.so" "ableton@$move_host:$remote/"
ssh -o BatchMode=yes "ableton@$move_host" "chmod 755 '$remote/test_stranger'; '$remote/test_stranger' '$remote/stranger.so'"
