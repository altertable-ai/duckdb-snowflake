#!/bin/bash

# DuckDB Snowflake ADBC Driver Installer
# Usage: curl -sSL https://raw.githubusercontent.com/iqea-ai/duckdb-snowflake/main/scripts/install-adbc-driver.sh | sh
#    or: wget -qO- https://raw.githubusercontent.com/iqea-ai/duckdb-snowflake/main/scripts/install-adbc-driver.sh | sh

set -e

# Configuration
# The Snowflake driver was split out of apache/arrow-adbc into its own repo
# (adbc-drivers/snowflake) with an independent release line (go/vX.Y.Z). The
# apache/arrow-adbc wheels are deprecated and lack GeoArrow GEOGRAPHY/GEOMETRY
# support, which shipped in go/v1.11.0.
DRIVER_VERSION="1.11.0"
RELEASE_TAG="go/v${DRIVER_VERSION}"
BASE_URL="https://github.com/adbc-drivers/snowflake/releases/download/${RELEASE_TAG}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

# Detect platform
detect_platform() {
    OS="$(uname -s)"
    ARCH="$(uname -m)"

    case "$OS" in
        Linux)
            if [ "$ARCH" = "x86_64" ]; then
                ASSET_NAME="snowflake_linux_amd64_v${DRIVER_VERSION}.tar.gz"
                LIB_IN_TARBALL="libadbc_driver_snowflake.so"
                PLATFORM="linux_amd64"
            elif [ "$ARCH" = "aarch64" ]; then
                ASSET_NAME="snowflake_linux_arm64_v${DRIVER_VERSION}.tar.gz"
                LIB_IN_TARBALL="libadbc_driver_snowflake.so"
                PLATFORM="linux_arm64"
            else
                print_error "Unsupported Linux architecture: $ARCH"
                exit 1
            fi
            ;;
        Darwin)
            if [ "$ARCH" = "arm64" ]; then
                ASSET_NAME="snowflake_macos_arm64_v${DRIVER_VERSION}.tar.gz"
                LIB_IN_TARBALL="libadbc_driver_snowflake.dylib"
                PLATFORM="osx_arm64"
            else
                print_error "Unsupported macOS architecture: $ARCH"
                echo "The ADBC Driver Foundry ships no macOS x86_64 (Intel) build."
                echo "Build the driver from source (https://github.com/adbc-drivers/snowflake)"
                echo "and set SNOWFLAKE_ADBC_DRIVER_PATH to the result."
                exit 1
            fi
            ;;
        MINGW*|CYGWIN*|MSYS*|Windows*)
            ASSET_NAME="snowflake_windows_amd64_v${DRIVER_VERSION}.tar.gz"
            LIB_IN_TARBALL="adbc_driver_snowflake.dll"
            PLATFORM="windows_amd64"
            ;;
        *)
            print_error "Unsupported operating system: $OS"
            exit 1
            ;;
    esac

    print_info "Detected platform: $OS $ARCH ($PLATFORM)"
}

# Detect DuckDB version
get_duckdb_version() {
    # Check if DuckDB is installed
    if ! command -v duckdb >/dev/null 2>&1; then
        print_error "DuckDB is not installed!"
        echo ""
        echo "Please install DuckDB first:"
        echo "  • macOS:  brew install duckdb"
        echo "  • Ubuntu: sudo apt-get install duckdb"
        echo "  • Other:  https://duckdb.org/docs/installation/"
        echo ""
        exit 1
    fi

    # Get DuckDB version
    DUCKDB_VERSION=$(duckdb -c "SELECT version()" 2>/dev/null | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    if [ -z "$DUCKDB_VERSION" ]; then
        print_error "Could not detect DuckDB version"
        echo "Please ensure DuckDB is properly installed and accessible"
        exit 1
    fi

    print_info "Detected DuckDB version: $DUCKDB_VERSION"
}

# Get platform string for DuckDB directory structure
get_duckdb_platform() {
    case "$PLATFORM" in
        linux_amd64)
            DUCKDB_PLATFORM="linux_amd64"
            ;;
        linux_arm64)
            DUCKDB_PLATFORM="linux_arm64"
            ;;
        osx_amd64)
            DUCKDB_PLATFORM="osx_amd64"
            ;;
        osx_arm64)
            DUCKDB_PLATFORM="osx_arm64"
            ;;
        windows_amd64)
            DUCKDB_PLATFORM="windows_amd64"
            ;;
        *)
            DUCKDB_PLATFORM="$PLATFORM"
            ;;
    esac
}

# Check for required tools
check_dependencies() {
    # Check for curl or wget
    if command -v curl >/dev/null 2>&1; then
        DOWNLOAD_CMD="curl -L -o"
    elif command -v wget >/dev/null 2>&1; then
        DOWNLOAD_CMD="wget -O"
    else
        print_error "Neither curl nor wget found. Please install one of them."
        exit 1
    fi

    # Check for tar (foundry releases are tarballs)
    if ! command -v tar >/dev/null 2>&1; then
        print_error "tar is required but not installed."
        exit 1
    fi
}

# Download the release tarball
download_driver() {
    ASSET_URL="${BASE_URL}/${ASSET_NAME}"
    ASSET_PATH="${INSTALL_DIR}/${ASSET_NAME}"

    if [ -f "${ASSET_PATH}" ]; then
        print_warning "Driver tarball already exists, checking if extraction is needed..."
    else
        print_info "Downloading ADBC Snowflake driver..."
        print_info "URL: ${ASSET_URL}"

        $DOWNLOAD_CMD "${ASSET_PATH}" "${ASSET_URL}"

        if [ $? -ne 0 ]; then
            print_error "Failed to download ADBC driver"
            print_info "Please check your internet connection and try again"
            exit 1
        fi

        print_success "Downloaded ${ASSET_NAME}"
    fi
}

# Global variable for driver filename
DRIVER_FILE=""

# Extract the driver from the tarball
extract_driver() {
    print_info "Extracting driver library..."

    cd "${INSTALL_DIR}"

    tar xzf "${ASSET_NAME}" "${LIB_IN_TARBALL}" 2>/dev/null || tar xzf "${ASSET_NAME}" 2>/dev/null

    if [ ! -f "${LIB_IN_TARBALL}" ]; then
        # Some tarballs nest the lib; find it.
        FOUND="$(find . -name "${LIB_IN_TARBALL}" -type f | head -1)"
        if [ -n "$FOUND" ]; then
            mv -f "$FOUND" "${LIB_IN_TARBALL}"
        else
            print_error "Failed to find ${LIB_IN_TARBALL} in ${ASSET_NAME}"
            exit 1
        fi
    fi

    # The extension resolves the driver by the fixed name
    # libadbc_driver_snowflake.so on every platform (see CMakeLists.txt), so
    # normalize the macOS .dylib / Windows .dll to that name.
    DRIVER_FILE="libadbc_driver_snowflake.so"
    if [ "${LIB_IN_TARBALL}" != "${DRIVER_FILE}" ]; then
        mv -f "${LIB_IN_TARBALL}" "${DRIVER_FILE}"
    fi

    # Make the library executable (important for some platforms)
    chmod +x "${DRIVER_FILE}" 2>/dev/null || true

    print_success "Extracted ${DRIVER_FILE}"
}


# Verify installation
verify_installation() {
    print_info "Verifying installation..."

    FINAL_PATH="${INSTALL_DIR}/${DRIVER_FILE}"
    if [ -f "${FINAL_PATH}" ]; then
        FILE_SIZE=$(ls -lh "${FINAL_PATH}" | awk '{print $5}')
        print_success "ADBC Snowflake driver installed successfully!"
        echo ""
        echo "Installation details:"
        echo "  • Driver: ${DRIVER_FILE}"
        echo "  • Location: ${FINAL_PATH}"
        echo "  • Size: ${FILE_SIZE}"
        echo "  • Version: ${DRIVER_VERSION}"
        echo ""

        # Platform-specific instructions
        case "$PLATFORM" in
            linux*)
                echo "To use with DuckDB Snowflake extension:"
                echo "  1. The extension will automatically find the driver at:"
                echo "     ${FINAL_PATH}"
                echo "  2. If you have issues, set the environment variable:"
                echo "     export SNOWFLAKE_ADBC_DRIVER_PATH=\"${FINAL_PATH}\""
                ;;
            osx*)
                echo "To use with DuckDB Snowflake extension:"
                echo "  1. The extension will automatically find the driver at:"
                echo "     ${FINAL_PATH}"
                echo "  2. If you have issues, set the environment variable:"
                echo "     export SNOWFLAKE_ADBC_DRIVER_PATH=\"${FINAL_PATH}\""
                ;;
            windows*)
                echo "To use with DuckDB Snowflake extension:"
                echo "  1. The extension will automatically find the driver at:"
                echo "     ${FINAL_PATH}"
                echo "  2. If you have issues, set the environment variable:"
                echo "     set SNOWFLAKE_ADBC_DRIVER_PATH=${FINAL_PATH}"
                ;;
        esac

        echo ""
        print_success "Installation complete! You can now use the Snowflake extension."
    else
        print_error "Installation verification failed"
        exit 1
    fi
}

# Main installation flow
main() {
    detect_platform
    check_dependencies
    get_duckdb_version

    echo ""
    echo "════════════════════════════════════════════════════════════════"
    echo "     DuckDB Snowflake ADBC Driver Installer"
    echo "     DuckDB Version: ${DUCKDB_VERSION}"
    echo "════════════════════════════════════════════════════════════════"
    echo ""

    get_duckdb_platform

    # Determine installation directory
    if [ -n "$DUCKDB_EXTENSION_DIR" ]; then
        INSTALL_DIR="$DUCKDB_EXTENSION_DIR"
    else
        # Get home directory
        if [ -n "$HOME" ]; then
            HOME_DIR="$HOME"
        else
            HOME_DIR="$(cd ~ && pwd)"
        fi

        # Use the same directory structure as DuckDB extensions
        # ~/.duckdb/extensions/<version>/<platform>/
        INSTALL_DIR="${HOME_DIR}/.duckdb/extensions/${DUCKDB_VERSION}/${DUCKDB_PLATFORM}"
    fi

    # Create directory if it doesn't exist
    mkdir -p "$INSTALL_DIR"
    print_info "Installation directory: $INSTALL_DIR"

    download_driver
    extract_driver
    verify_installation
}

# Run main function
main "$@"