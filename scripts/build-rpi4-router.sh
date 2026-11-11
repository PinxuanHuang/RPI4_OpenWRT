#!/bin/sh
# Build the local packages with the matching release SDK, then assemble images
# using the official release kernel/packages. Does not flash any device.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
RELEASE=25.12.6
BASE=https://downloads.openwrt.org/releases/$RELEASE/targets/bcm27xx/bcm2711
WORK=$ROOT/build_dir/router-integration
SDK_NAME=openwrt-sdk-$RELEASE-bcm27xx-bcm2711_gcc-14.3.0_musl.Linux-x86_64
IB_NAME=openwrt-imagebuilder-$RELEASE-bcm27xx-bcm2711.Linux-x86_64
SDK=$WORK/$SDK_NAME
IB=$WORK/$IB_NAME
OUT=$ROOT/bin/targets/bcm27xx/bcm2711

[ -s files/etc/config/rpi4_router ] || {
	echo 'Create files/etc/config/rpi4_router with SSID/country/radio/key first (see docs/rpi4-router.md).' >&2
	exit 1
}
mkdir -p "$WORK" "$OUT" dl logs

fetch() {
	name=$1 hash=$2
	if [ ! -f "dl/$name.tar.zst" ]; then
		curl -fL --retry 3 "$BASE/$name.tar.zst" -o "dl/$name.tar.zst.part"
		mv "dl/$name.tar.zst.part" "dl/$name.tar.zst"
	fi
	printf '%s  %s\n' "$hash" "dl/$name.tar.zst" | sha256sum -c -
	[ -d "$WORK/$name" ] || tar --zstd -xf "dl/$name.tar.zst" -C "$WORK"
}
fetch "$SDK_NAME" d5faf36e92bc7311f138607606e8dd9fc4a6bda3fcf5277b0a5647515758df8e
fetch "$IB_NAME" 78191c1909358925bd92fc2200da29c1e4a0a8a949a4af77bd97650471604d2a

cp -R package/utils/router-telemetry "$SDK/package/"
cp -R package/network/config/rpi4-router "$SDK/package/"
cat >> "$SDK/.config" <<'EOF'
CONFIG_ALL_NONSHARED=n
CONFIG_ALL_KMODS=n
CONFIG_ALL=n
CONFIG_PACKAGE_router-telemetry=m
CONFIG_PACKAGE_rpi4-router=m
EOF
make -C "$SDK" defconfig > logs/router-sdk-config.log 2>&1
make -C "$SDK" package/router-telemetry/clean package/rpi4-router/clean > logs/router-sdk-build.log 2>&1
make -C "$SDK" package/router-telemetry/compile package/rpi4-router/compile -j2 V=s >> logs/router-sdk-build.log 2>&1

mkdir -p "$IB/packages" "$OUT/packages"
for package in router-telemetry-1.0-r2.apk rpi4-router-1.0-r1.apk; do
	cp "$SDK/bin/packages/aarch64_cortex-a72/base/$package" "$IB/packages/"
	cp "$SDK/bin/packages/aarch64_cortex-a72/base/$package" "$OUT/packages/"
done
rm -f "$IB/packages/router-telemetry-1.0-r1.apk" "$OUT/packages/router-telemetry-1.0-r1.apk"
# Use the same conditional serial-console removal as full source builds.
cp target/linux/bcm27xx/image/Makefile "$IB/target/linux/bcm27xx/image/Makefile"
# ImageBuilder clears MAKEFLAGS before invoking its image sub-make, so a
# command-line CONFIG_PACKAGE override alone does not reach boot-common.
sed -i '/^CONFIG_PACKAGE_rpi4-router=/d' "$IB/.config"
printf '\nCONFIG_PACKAGE_rpi4-router=y\n' >> "$IB/.config"
make -C "$IB" image PROFILE=rpi-4 \
	PACKAGES='router-telemetry rpi4-router' FILES="$ROOT/files" > logs/router-image-build.log 2>&1
cp "$IB/bin/targets/bcm27xx/bcm2711/"* "$OUT/"

# Copy the stripped binary installed in the assembled firmware root.
install -m 755 "$IB/build_dir/target-aarch64_cortex-a72_musl/root-bcm27xx/usr/sbin/router_telemetry_sender" "$OUT/router_telemetry_sender"
rm -f "$OUT/router_telemetry"
file "$OUT/router_telemetry_sender"
echo "Images, packages and binary: $OUT"
echo 'Build logs: logs/router-sdk-build.log and logs/router-image-build.log'
