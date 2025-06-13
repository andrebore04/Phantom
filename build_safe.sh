#!/bin/bash

# Enhanced build script for Phantom with safety flags
# This script adds additional compiler flags to catch potential issues

echo "Starting enhanced Phantom build with safety checks..."

# Set build configuration
BUILD_CONFIG="Release"
if [ "$1" = "debug" ]; then
    BUILD_CONFIG="Debug"
fi

# Additional safety compiler flags
EXTRA_CFLAGS=(
    "-fstack-protector-strong"          # Stack protection
    "-Wformat=2"                        # Enhanced format string checking
    "-Wformat-security"                 # Security format warnings
    "-Wnull-dereference"               # Null pointer dereference warnings
    "-Warray-bounds"                   # Array bounds checking
    "-Wstringop-overflow"              # String operation overflow
    "-fno-delete-null-pointer-checks"  # Don't optimize away null checks
    "-fwrapv"                          # Define overflow behavior
    "-Werror=return-type"              # Error on missing return values
    "-Werror=implicit-function-declaration" # Error on undeclared functions
    "-Wall"                            # All warnings
    "-Wextra"                          # Extra warnings
)

# Kernel-specific safety flags
KERNEL_SAFETY_FLAGS=(
    "-mno-red-zone"                    # Required for kernel code
    "-fno-builtin"                     # Don't use builtin functions
    "-fno-stack-protector"             # Kernel handles its own protection
    "-DKERNEL"                         # Kernel compilation flag
    "-D__KERNEL__"                     # Alternative kernel flag
)

echo "Build configuration: $BUILD_CONFIG"
echo "Extra safety flags enabled"

# Build with xcodebuild
if command -v xcodebuild &> /dev/null; then
    echo "Building with xcodebuild..."
    
    # Join arrays into space-separated strings
    EXTRA_CFLAGS_STR=$(IFS=" "; echo "${EXTRA_CFLAGS[*]}")
    KERNEL_SAFETY_FLAGS_STR=$(IFS=" "; echo "${KERNEL_SAFETY_FLAGS[*]}")
    
    xcodebuild -project Phantom.xcodeproj \
        -configuration $BUILD_CONFIG \
        CLANG_WARN_SUSPICIOUS_IMPLICIT_CONVERSION=YES \
        CLANG_WARN_EMPTY_BODY=YES \
        CLANG_WARN_CONDITIONAL_ASSIGNMENT=YES \
        GCC_WARN_UNINITIALIZED_AUTOS=YES_AGGRESSIVE \
        OTHER_CFLAGS="$EXTRA_CFLAGS_STR $KERNEL_SAFETY_FLAGS_STR" \
        build
    
    BUILD_RESULT=$?
else
    echo "Error: xcodebuild not found. Please install Xcode Command Line Tools."
    exit 1
fi

if [ $BUILD_RESULT -eq 0 ]; then
    echo "✅ Build successful with enhanced safety checks"
    
    # Check if the kext was built
    KEXT_PATH="build/$BUILD_CONFIG/Phantom.kext"
    if [ -d "$KEXT_PATH" ]; then
        echo "📦 Phantom.kext built at: $KEXT_PATH"
        
        # Validate the kext structure
        if [ -f "$KEXT_PATH/Contents/MacOS/Phantom" ]; then
            echo "✅ Kext binary found"
            
            # Check code signature (will fail but shows structure)
            echo "🔍 Checking kext structure..."
            codesign -dvvv "$KEXT_PATH" 2>/dev/null || echo "⚠️  Kext is not signed (expected for development)"
            
            # Show binary info
            file "$KEXT_PATH/Contents/MacOS/Phantom"
            
        else
            echo "❌ Kext binary not found"
        fi
    else
        echo "❌ Phantom.kext not found at expected location"
    fi
else
    echo "❌ Build failed with error code: $BUILD_RESULT"
    echo "Check the build output above for specific errors"
    exit 1
fi

echo "Enhanced build process completed."
