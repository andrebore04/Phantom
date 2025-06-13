#!/bin/bash

# Phantom Safety Testing Script
# This script helps test the safety improvements

echo "🧪 Phantom Safety Testing Script"
echo "================================"

# Check if we're running as root
if [ "$EUID" -ne 0 ]; then
    echo "❌ This script must be run as root (sudo)"
    exit 1
fi

# Function to check kext status
check_kext_status() {
    echo "📋 Checking kext status..."
    
    if kextstat | grep -q "Phantom"; then
        echo "✅ Phantom kext is loaded"
        kextstat | grep Phantom
    else
        echo "❌ Phantom kext is not loaded"
        return 1
    fi
    
    if kextstat | grep -q "Lilu"; then
        echo "✅ Lilu kext is loaded"
        kextstat | grep Lilu
    else
        echo "❌ Lilu kext is not loaded"
        return 1
    fi
}

# Function to test sysctl access
test_sysctl_access() {
    echo ""
    echo "🔍 Testing sysctl access..."
    
    # Test VMM present
    if command -v sysctl &> /dev/null; then
        echo "Testing kern.hv_vmm_present..."
        if sysctl kern.hv_vmm_present 2>/dev/null; then
            echo "✅ VMM sysctl access successful"
        else
            echo "⚠️  VMM sysctl access failed or not available"
        fi
        
        echo "Testing kern.securelevel..."
        if sysctl kern.securelevel 2>/dev/null; then
            echo "✅ SecureLevel sysctl access successful"
        else
            echo "⚠️  SecureLevel sysctl access failed or not available"
        fi
    else
        echo "❌ sysctl command not found"
    fi
}

# Function to monitor kernel logs
monitor_logs() {
    echo ""
    echo "📊 Monitoring recent kernel logs for Phantom..."
    
    # Check recent kernel messages
    if dmesg | grep -i phantom | tail -10; then
        echo "✅ Found Phantom log entries"
    else
        echo "⚠️  No recent Phantom log entries found"
    fi
    
    # Check for panic or error messages
    echo ""
    echo "🚨 Checking for panic/error messages..."
    if dmesg | grep -iE "(panic|fault|crash)" | grep -i phantom | tail -5; then
        echo "❌ Found potential error messages related to Phantom"
    else
        echo "✅ No panic/error messages found"
    fi
}

# Function to test process detection
test_process_detection() {
    echo ""
    echo "👥 Testing process detection..."
    
    # Start a background process that might trigger filtering
    echo "Starting test process..."
    (sleep 5 && sysctl kern.hv_vmm_present >/dev/null 2>&1) &
    TEST_PID=$!
    
    sleep 1
    
    if ps -p $TEST_PID >/dev/null 2>&1; then
        echo "✅ Test process running (PID: $TEST_PID)"
    else
        echo "⚠️  Test process already completed"
    fi
    
    # Clean up
    kill $TEST_PID 2>/dev/null || true
    wait $TEST_PID 2>/dev/null || true
}

# Function to validate kext integrity
validate_kext_integrity() {
    echo ""
    echo "🔐 Validating kext integrity..."
    
    KEXT_PATH="/Library/Extensions/Phantom.kext"
    if [ -d "$KEXT_PATH" ]; then
        echo "✅ Phantom.kext found at $KEXT_PATH"
        
        # Check Info.plist
        if [ -f "$KEXT_PATH/Contents/Info.plist" ]; then
            echo "✅ Info.plist exists"
        else
            echo "❌ Info.plist missing"
        fi
        
        # Check binary
        if [ -f "$KEXT_PATH/Contents/MacOS/Phantom" ]; then
            echo "✅ Phantom binary exists"
            file "$KEXT_PATH/Contents/MacOS/Phantom"
        else
            echo "❌ Phantom binary missing"
        fi
    else
        echo "❌ Phantom.kext not found at $KEXT_PATH"
    fi
}

# Main testing sequence
main() {
    echo "Starting safety tests..."
    echo ""
    
    validate_kext_integrity
    check_kext_status
    test_sysctl_access
    test_process_detection
    monitor_logs
    
    echo ""
    echo "🏁 Testing completed"
    echo ""
    echo "💡 Tips:"
    echo "- If any tests fail, check the logs with: sudo dmesg | grep -i phantom"
    echo "- To enable debug logging, add '-phantomdbg' to boot-args"
    echo "- For emergency removal: sudo rm -rf /Library/Extensions/Phantom.kext"
}

# Run tests
main "$@"
