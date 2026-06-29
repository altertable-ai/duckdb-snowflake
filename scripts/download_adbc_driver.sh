#!/bin/bash

# Download the pre-built ADBC Snowflake driver from the ADBC Driver Foundry.
#
# The Snowflake driver was split out of apache/arrow-adbc into its own repo
# (adbc-drivers/snowflake) with an independent release line (go/vX.Y.Z). We track
# the foundry build because native GeoArrow GEOGRAPHY/GEOMETRY support (#117) ships
# there, not in the apache/arrow-adbc wheels.
#
# NOTE: the foundry does not publish a macOS x86_64 (Intel) artifact. On that
# platform this script errors; build the driver from source or stay on the older
# apache/arrow-adbc wheel if Intel macOS support is required.

DRIVER_VERSION="1.11.0"
RELEASE_TAG="go/v${DRIVER_VERSION}"
BASE_URL="https://github.com/adbc-drivers/snowflake/releases/download/${RELEASE_TAG}"

OS="$(uname -s)"
ARCH="$(uname -m)"
echo "Detected platform: $OS $ARCH"

DRIVER_DIR="$(dirname "$0")/../adbc_drivers"
mkdir -p "$DRIVER_DIR"

# Map platform -> foundry asset name and the library filename inside the tarball.
case "$OS" in
    Linux)
        if [ "$ARCH" = "x86_64" ]; then
            ASSET="snowflake_linux_amd64_v${DRIVER_VERSION}.tar.gz"
        elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
            ASSET="snowflake_linux_arm64_v${DRIVER_VERSION}.tar.gz"
        else
            echo "Unsupported Linux architecture: $ARCH"; exit 1
        fi
        LIB_IN_TARBALL="libadbc_driver_snowflake.so"
        ;;
    Darwin)
        if [ "$ARCH" = "arm64" ]; then
            ASSET="snowflake_macos_arm64_v${DRIVER_VERSION}.tar.gz"
        else
            echo "Unsupported macOS architecture: $ARCH (the ADBC foundry ships no macOS x86_64 build)"; exit 1
        fi
        LIB_IN_TARBALL="libadbc_driver_snowflake.dylib"
        ;;
    MINGW*|CYGWIN*|MSYS*|Windows*)
        ASSET="snowflake_windows_amd64_v${DRIVER_VERSION}.tar.gz"
        LIB_IN_TARBALL="adbc_driver_snowflake.dll"
        ;;
    *)
        echo "Unsupported operating system: $OS"; exit 1
        ;;
esac

ASSET_URL="${BASE_URL}/${ASSET}"
ASSET_PATH="${DRIVER_DIR}/${ASSET}"

if [ ! -f "${ASSET_PATH}" ]; then
    echo "Downloading ${ASSET}..."
    curl -L -f -o "${ASSET_PATH}" "${ASSET_URL}"
    if [ $? -ne 0 ]; then
        echo "Failed to download ADBC driver from ${ASSET_URL}"; exit 1
    fi
else
    echo "Asset already exists: ${ASSET_PATH}"
fi

echo "Extracting driver library..."
cd "${DRIVER_DIR}" || exit 1
tar xzf "${ASSET}" "${LIB_IN_TARBALL}" 2>/dev/null || tar xzf "${ASSET}" 2>/dev/null

if [ ! -f "${LIB_IN_TARBALL}" ]; then
    # Some tarballs nest the lib; find it.
    FOUND="$(find . -name "${LIB_IN_TARBALL}" -type f | head -1)"
    if [ -n "$FOUND" ]; then
        mv "$FOUND" "${LIB_IN_TARBALL}"
    else
        echo "Failed to find ${LIB_IN_TARBALL} in ${ASSET}"; exit 1
    fi
fi

# The CMake build links against the fixed name libadbc_driver_snowflake.so on all
# platforms (see CMakeLists.txt), so normalize to that.
if [ "${LIB_IN_TARBALL}" != "libadbc_driver_snowflake.so" ]; then
    mv -f "${LIB_IN_TARBALL}" "libadbc_driver_snowflake.so"
fi
chmod +x "libadbc_driver_snowflake.so" 2>/dev/null || true

cd - > /dev/null
echo "ADBC driver ${DRIVER_VERSION} setup complete!"
echo "Driver location: ${DRIVER_DIR}/libadbc_driver_snowflake.so"
