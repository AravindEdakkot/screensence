#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs a built .deb in a clean container and checks the result.
#
#   scripts/test-deb.sh ubuntu:24.04 [--camera]
#
# Uses the package from build-deb/<image>/. With --camera, the container also
# runs `screensense debug` on /dev/video0 for a few seconds, pausing the
# ScreenSense service on this computer meanwhile so the camera is free.
set -uo pipefail
IMAGE=${1:?usage: $0 IMAGE [--camera]}
CAMERA=${2:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/build-deb/${IMAGE//[:\/]/-}"
NAME="ss-deb-test-$$"
pass=0; fail=0
check() {  # check NAME COMMAND...
    local out
    if out=$("${@:2}" 2>&1); then echo "PASS  $1"; pass=$((pass+1)); else echo "FAIL  $1: $out" | head -5; fail=$((fail+1)); fi
}
run() { docker exec "$NAME" bash -c "$1"; }

DEVICE=()
[ "$CAMERA" = "--camera" ] && DEVICE=(--device /dev/video0)
# The container ends itself; `docker stop` is not always permitted.
check "manual page in the package" grep -q "man1/screensense.1" <(dpkg-deb -c "$(ls "$DIR"/screensense_*_amd64.deb)")

docker run -d --rm --init --name "$NAME" "${DEVICE[@]}" -v "$DIR":/debs:ro "$IMAGE" sleep 1800 >/dev/null
trap 'docker exec "$NAME" pkill sleep >/dev/null 2>&1' EXIT

run 'export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && apt-get install -y -qq /debs/screensense_*_amd64.deb' > "$DIR/install.log" 2>&1
check "package installs with its dependencies" run 'dpkg -s screensense | grep -q "Status: install ok installed"'
check "program runs" run 'screensense --version'
check "help works" run 'screensense --help | grep -q calibrate'
check "face model installed" run 'ls /usr/share/screensense/models/face_detection_yunet_*.onnx'
check "eye model and its licence installed" run 'test -f /usr/share/screensense/models/iris_landmark.onnx && test -f /usr/share/screensense/models/LICENSE.iris_landmark'
check "user service installed" run 'test -f /usr/lib/systemd/user/screensense.service'
check "user service starts at login" run 'test -L /etc/systemd/user/graphical-session.target.wants/screensense.service'
check "extension installed" run 'test -f /usr/share/gnome-shell/extensions/screensense@aravindedakkot.gmail.com/extension.js'
check "copyright file installed" run 'grep -q Apache-2.0 /usr/share/doc/screensense/copyright'
check "status without a running service" run 'screensense status; [ $? -eq 3 ]'
echo "      $(run 'screensense --version') on $(run '. /etc/os-release; echo $PRETTY_NAME'), models: $(run 'ls /usr/share/screensense/models | tr "\n" " "')"

if [ "$CAMERA" = "--camera" ]; then
    before=$(screensense status 2>/dev/null || true)
    [ "$before" = paused ] || screensense pause >/dev/null 2>&1
    sleep 1
    out=$(run 'timeout -s INT 10 screensense debug --backend null 2>&1 | tr "\r" "\n"')
    [ "$before" = paused ] || screensense resume >/dev/null 2>&1
    echo "$out" | grep -E "^(face|no face)" | sed -n '20p;40p;60p' | cut -c1-110 | sed 's/^/      /'
    check "camera opens" grep -q "camera /dev/video0 on" <<<"$out"
    check "face found, eyes read" grep -qE "^face .* eye [+-]" <<<"$out"
fi

run 'export DEBIAN_FRONTEND=noninteractive; apt-get remove -y -qq screensense' >> "$DIR/install.log" 2>&1
check "package removes cleanly" run '! test -e /usr/bin/screensense && ! test -e /usr/share/gnome-shell/extensions/screensense@aravindedakkot.gmail.com'
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
