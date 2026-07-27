#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
APP_DIR="${ROOT_DIR}/build/AppDir"

echo "=========================================="
echo " 🎛️  Building RigRoom AppImage"
echo "=========================================="

# Build on Ubuntu 22.04 so the AppImage uses that glibc baseline. Do not bundle glibc.
BASELINE_IMAGE="${RIGROOM_APPIMAGE_BASELINE_IMAGE:-ubuntu@sha256:0d779ea97881505f5ef0039336ee85edba27519bdba968c284c86ee066a973c8}"
if [ -z "${IN_BUILD_CONTAINER}" ] && (command -v podman >/dev/null 2>&1 || command -v docker >/dev/null 2>&1); then
    CONTAINER_TOOL=$(command -v podman 2>/dev/null || command -v docker 2>/dev/null)
    echo "--> Delegating compilation to ${BASELINE_IMAGE} via ${CONTAINER_TOOL}..."
    exec "${CONTAINER_TOOL}" run --rm \
        -v "${ROOT_DIR}:/workspace:Z" \
        -w /workspace \
        -e IN_BUILD_CONTAINER=1 \
        "${BASELINE_IMAGE}" \
        bash -c "DEBIAN_FRONTEND=noninteractive apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake pkg-config qt6-base-dev qt6-base-private-dev libgl1-mesa-dev libjack-jackd2-dev liblilv-dev libsuil-dev libsecret-1-dev libx11-dev curl file && ./packaging/build-appimage.sh"
fi

# 1. Compile RigRoom in Release mode
echo "--> Compiling RigRoom binary..."
rm -rf "${BUILD_DIR}/CMakeCache.txt" "${BUILD_DIR}/CMakeFiles"
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release -DRIGROOM_DEV_BUILD=OFF

# Calculate safe parallel jobs based on available RAM (allocating ~2GB per job) to prevent OOM system lockups
NPROC=$(nproc)
TOTAL_RAM_GB=$(free -g | awk '/^Mem:/{print $2}')
if [ -z "${TOTAL_RAM_GB}" ] || [ "${TOTAL_RAM_GB}" -lt 1 ]; then TOTAL_RAM_GB=4; fi
MAX_JOBS=$(( TOTAL_RAM_GB / 2 ))
if [ "$MAX_JOBS" -lt 1 ]; then MAX_JOBS=1; fi
JOBS=$NPROC
if [ "$JOBS" -gt "$MAX_JOBS" ]; then JOBS=$MAX_JOBS; fi
echo "--> Building with ${JOBS} parallel job(s) (RAM: ${TOTAL_RAM_GB}GB, Cores: ${NPROC})..."

cmake --build "${BUILD_DIR}" --config Release -j"${JOBS}"

# 2. Setup AppDir structure
echo "--> Preparing AppDir structure..."
rm -rf "${APP_DIR}"
mkdir -p "${APP_DIR}/usr/bin"
mkdir -p "${APP_DIR}/usr/lib"
mkdir -p "${APP_DIR}/usr/plugins/platforms"
mkdir -p "${APP_DIR}/usr/share/applications"

# Copy binary & launcher
cp "${BUILD_DIR}/RigRoom" "${APP_DIR}/usr/bin/RigRoom"
cp "${SCRIPT_DIR}/AppRun" "${APP_DIR}/AppRun"
chmod +x "${APP_DIR}/AppRun"

# Copy Qt plugins & themes
echo "--> Bundling Qt plugins & themes..."
QT_PLUGINS_DIR=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || qmake -query QT_INSTALL_PLUGINS 2>/dev/null || echo "/usr/lib64/qt6/plugins")
for plugin_dir in platforms platformthemes iconengines imageformats styles; do
    if [ -d "${QT_PLUGINS_DIR}/${plugin_dir}" ]; then
        mkdir -p "${APP_DIR}/usr/plugins/${plugin_dir}"
        cp -r "${QT_PLUGINS_DIR}/${plugin_dir}"/* "${APP_DIR}/usr/plugins/${plugin_dir}/" 2>/dev/null || true
    fi
done

# Copy Suil modules into AppDir/usr/lib/suil-0
echo "--> Bundling Suil plugin modules..."
for suil_dir in /usr/lib64/suil-0 /usr/lib/x86_64-linux-gnu/suil-0 /usr/lib/suil-0; do
    if [ -d "$suil_dir" ]; then
        mkdir -p "${APP_DIR}/usr/lib/suil-0"
        cp -r "$suil_dir"/* "${APP_DIR}/usr/lib/suil-0/" 2>/dev/null || true
    fi
done

# Copy shared library dependencies into AppDir/usr/lib
echo "--> Bundling dynamic shared libraries into AppDir..."
mkdir -p "${APP_DIR}/usr/lib"

EXCLUDE_REGEX="libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libgcc_s\.so|libstdc\+\+\.so|ld-linux|libpipewire|libjack|libspa|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libsystemd|libselinux|libresolv|libmount|libblkid|libcom_err|libk5crypto|libgssapi_krb5|libkrb5|libkrb5support|libkeyutils|libz\.so|libffi|libcrypto|libssl|libcurl|libdbus-1|libpcre2"

# Copy direct dependencies of RigRoom
for lib in $(ldd "${BUILD_DIR}/RigRoom" | awk '{print $3}' | grep '^/'); do
    libname=$(basename "$lib")
    if ! echo "$libname" | grep -qE "$EXCLUDE_REGEX"; then
        cp -L "$lib" "${APP_DIR}/usr/lib/" 2>/dev/null || true
    fi
done

# Copy dependencies of Qt platform plugins
if [ -d "${APP_DIR}/usr/plugins/platforms" ]; then
    for plugin in "${APP_DIR}/usr/plugins/platforms"/*.so; do
        if [ -f "$plugin" ]; then
            for lib in $(ldd "$plugin" 2>/dev/null | awk '{print $3}' | grep '^/'); do
                libname=$(basename "$lib")
                if ! echo "$libname" | grep -qE "$EXCLUDE_REGEX"; then
                    cp -L "$lib" "${APP_DIR}/usr/lib/" 2>/dev/null || true
                fi
            done
        fi
    done
fi

# Multi-pass recursive dependency scanner to ensure all sub-dependencies are bundled
for pass in 1 2; do
    for libfile in "${APP_DIR}/usr/lib"/*.so*; do
        if [ -f "$libfile" ] && [ ! -L "$libfile" ]; then
            for lib in $(ldd "$libfile" 2>/dev/null | awk '{print $3}' | grep '^/'); do
                libname=$(basename "$lib")
                if ! echo "$libname" | grep -qE "$EXCLUDE_REGEX"; then
                    cp -L "$lib" "${APP_DIR}/usr/lib/" 2>/dev/null || true
                fi
            done
        fi
    done
done

# Build fallback libjack.so.0 stub for systems without PipeWire/JACK installed
echo "--> Compiling libjack fallback stub for systems without JACK..."
mkdir -p "${APP_DIR}/usr/lib/fallback"
gcc -shared -fPIC "${SCRIPT_DIR}/libjack_fallback.c" -o "${APP_DIR}/usr/lib/fallback/libjack.so.0" 2>/dev/null || true

# Copy desktop file & app icon
cp "${SCRIPT_DIR}/org.rigroom.RigRoom.desktop" "${APP_DIR}/org.rigroom.RigRoom.desktop"
cp "${SCRIPT_DIR}/org.rigroom.RigRoom.desktop" "${APP_DIR}/usr/share/applications/org.rigroom.RigRoom.desktop"
if [ -f "${SCRIPT_DIR}/org.rigroom.RigRoom.png" ]; then
    cp "${SCRIPT_DIR}/org.rigroom.RigRoom.png" "${APP_DIR}/org.rigroom.RigRoom.png"
    mkdir -p "${APP_DIR}/usr/share/icons/hicolor/256x256/apps"
    cp "${SCRIPT_DIR}/org.rigroom.RigRoom.png" "${APP_DIR}/usr/share/icons/hicolor/256x256/apps/org.rigroom.RigRoom.png"
    ln -sf "org.rigroom.RigRoom.png" "${APP_DIR}/.DirIcon"
fi
if [ -f "${SCRIPT_DIR}/org.rigroom.RigRoom.appdata.xml" ]; then
    mkdir -p "${APP_DIR}/usr/share/metainfo"
    cp "${SCRIPT_DIR}/org.rigroom.RigRoom.appdata.xml" "${APP_DIR}/usr/share/metainfo/org.rigroom.RigRoom.appdata.xml"
fi

# 3. Fetch appimagetool if not available
APPIMAGETOOL="${ROOT_DIR}/build/appimagetool-x86_64.AppImage"
if [ ! -f "${APPIMAGETOOL}" ]; then
    echo "--> Downloading appimagetool..."
    curl -sL "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -o "${APPIMAGETOOL}"
    chmod +x "${APPIMAGETOOL}"
fi

# 4. Generate AppImage
RAW_APPIMAGE="${ROOT_DIR}/build/RigRoom-raw.AppImage"
APP_VERSION=$(grep '^#define RIGROOM_VERSION_STRING' "${BUILD_DIR}/Version.h" | cut -d '"' -f 2)
if [ -z "${APP_VERSION}" ]; then
    echo "ERROR: Could not determine RigRoom version from generated Version.h." >&2
    exit 1
fi
OUTPUT_APPIMAGE="${ROOT_DIR}/RigRoom-${APP_VERSION}-x86_64.AppImage"
echo "--> Generating ${OUTPUT_APPIMAGE}..."
export APPIMAGE_EXTRACT_AND_RUN=1
export NO_APPSTREAM=1
ARCH=x86_64 "${APPIMAGETOOL}" "${APP_DIR}" "${RAW_APPIMAGE}"

# 5. Create Universal Wrapper with Automatic FUSE 2 Bypass
echo "--> Packaging Universal Zero-Install AppImage..."
cat << 'EOF' > "${OUTPUT_APPIMAGE}"
#!/bin/bash
# RigRoom Universal AppImage Wrapper
# Auto-detects missing FUSE 2 library on modern Linux distros (Ubuntu 22.04+, 24.04+, Fedora, Arch)
SELF="$(readlink -f "$0")"
if ! ldconfig -p 2>/dev/null | grep -q "libfuse.so.2" && [ ! -f /lib/x86_64-linux-gnu/libfuse.so.2 ] && [ ! -f /usr/lib64/libfuse.so.2 ] && [ ! -f /usr/lib/libfuse.so.2 ]; then
    export APPIMAGE_EXTRACT_AND_RUN=1
fi
SKIP=$(awk '/^__ARCHIVE_FOLLOWS__/ { print NR + 1; exit 0; }' "$SELF")
tail -n +$SKIP "$SELF" > /tmp/rigroom_exec_$$ 2>/dev/null || true
if [ -s /tmp/rigroom_exec_$$ ]; then
    chmod +x /tmp/rigroom_exec_$$
    /tmp/rigroom_exec_$$ "$@"
    RET=$?
    rm -f /tmp/rigroom_exec_$$
    exit $RET
else
    rm -f /tmp/rigroom_exec_$$
    exec "$SELF" "$@"
fi
exit 0
__ARCHIVE_FOLLOWS__
EOF

cat "${RAW_APPIMAGE}" >> "${OUTPUT_APPIMAGE}"
chmod +x "${OUTPUT_APPIMAGE}"

echo "=========================================="
echo " ✅ Universal AppImage Created Successfully!"
echo " Output: ${OUTPUT_APPIMAGE}"
echo " Run with: ./RigRoom-${APP_VERSION}-x86_64.AppImage"
echo "=========================================="
