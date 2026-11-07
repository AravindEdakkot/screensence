#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the .deb for one distribution release inside a clean container, so the
# package links against exactly the libraries that release ships.
#
#   scripts/build-deb.sh ubuntu:24.04      (or debian:12, ubuntu:26.04, ...)
#
# Output: build-deb/<image>/screensense_<version>~<release>_<arch>.deb, for
# example screensense_0.2.0~ubuntu24.04_amd64.deb, plus a lintian report. The
# release in the version keeps the files apart on a download page, and lets a
# newer release's package upgrade an older one.
set -euo pipefail
IMAGE=${1:?usage: $0 IMAGE, for example ubuntu:24.04}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build-deb/${IMAGE//[:\/]/-}"
rm -rf "$OUT"
mkdir -p "$OUT"

# Pack the working tree, minus build output, so uncommitted changes are included.
tar -C "$ROOT" --exclude='./build*' --exclude='./.git' -czf "$OUT/source.tar.gz" .

docker run --rm -i -v "$OUT":/out -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$IMAGE" bash -s <<'INNER'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential debhelper cmake pkg-config \
    libopencv-dev libglib2.0-dev lintian >/dev/null
mkdir -p /build/screensense && cd /build/screensense
tar -xzf /out/source.tar.gz
. /etc/os-release
sed -i "1s/^screensense (\([^)]*\)) [a-z]*;/screensense (\1~${ID}${VERSION_ID}) ${VERSION_CODENAME};/" debian/changelog
head -1 debian/changelog
dpkg-buildpackage -us -uc -b -j4 > /out/build.log 2>&1 || { tail -40 /out/build.log; exit 1; }
cp ../*.deb /out/
lintian --info ../*.changes > /out/lintian.txt 2>&1 || true
rm /out/source.tar.gz
chown -R "$HOST_UID:$HOST_GID" /out
INNER
ls "$OUT"/*.deb

# A copy for the download page, without the "~" that GitHub may rename, and a
# checksum list covering every release built so far.
mkdir -p "$ROOT/build-deb/release"
for deb in "$OUT"/screensense_*.deb; do
    cp "$deb" "$ROOT/build-deb/release/$(basename "$deb" | tr '~' '_')"
done
(cd "$ROOT/build-deb/release" && sha256sum screensense_*.deb > SHA256SUMS)
