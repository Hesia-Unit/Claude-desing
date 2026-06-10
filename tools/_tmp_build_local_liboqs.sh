set -euo pipefail
PW='Matisse2008.'
ROOT=/mnt/c/Users/matis/Documents/Hesia-Firmware
LIBSRC=$HOME/liboqs-src
LIBBUILD=$HOME/liboqs-build
LIBINST=$HOME/liboqs

printf '%s\n' "$PW" | sudo -S apt-get update >/dev/null
printf '%s\n' "$PW" | sudo -S apt-get install -y ninja-build libssl-dev pkg-config rsync >/dev/null

rm -rf "$LIBSRC" "$LIBBUILD"
mkdir -p "$LIBSRC" "$LIBBUILD" "$LIBINST"
rsync -a --delete "$ROOT/server_source/liboqs/" "$LIBSRC/"

cmake -S "$LIBSRC" -B "$LIBBUILD" -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$LIBINST" \
  -DOQS_BUILD_ONLY_LIB=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DOQS_MINIMAL_BUILD='KEM_ml_kem_1024;SIG_ml_dsa_87'

cmake --build "$LIBBUILD" -j2
cmake --install "$LIBBUILD"

ls -l "$LIBINST/lib/liboqs.so"