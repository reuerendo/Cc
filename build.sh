#!/bin/bash

set -e

echo "Building Calibre Companion for PocketBook InkPad 4..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if SDK exists
if [ ! -d "SDK/SDK_6.3.0" ]; then
    echo -e "${YELLOW}PocketBook SDK not found. Downloading...${NC}"
    mkdir -p SDK
    cd SDK
    git clone --depth 1 --branch 5.19 https://github.com/pocketbook/SDK_6.3.0.git
    cd ..
    echo -e "${GREEN}SDK downloaded successfully.${NC}"
fi

# Create build directory
if [ ! -d "build" ]; then
    mkdir build
    echo -e "${GREEN}Build directory created.${NC}"
fi

# Configure CMake
echo -e "${YELLOW}Configuring CMake...${NC}"
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake "$@"

# Build
echo -e "${YELLOW}Building...${NC}"
cmake --build . --config Release -- -j$(nproc)

echo ""
echo -e "${GREEN}Build complete!${NC}"
echo -e "Output: ${GREEN}build/connect-to-calibre.app${NC}"
echo ""
echo "To install on device:"
echo "  1. Copy connect-to-calibre.app to /applications on device"
echo "  2. Restart device or open Applications menu"