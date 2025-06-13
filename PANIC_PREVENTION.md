# Phantom Kernel Panic Prevention Guide

## Issues Fixed

### 1. **Memory Access Validation**
- Added bounds checking for kernel addresses
- Implemented address alignment validation
- Enhanced null pointer checks throughout the codebase

### 2. **Sysctl Tree Traversal Safety**
- Added traversal limits to prevent infinite loops
- Enhanced null pointer validation for sysctl nodes
- Safe string comparison with length limits
- Proper validation of `oid_arg1` before casting

### 3. **Kernel Writing Operations**
- Replaced `PANIC_COND` with graceful error handling
- Added proper error checking for `MachInfo::setKernelWriting()`
- Implemented safe enable/disable patterns

### 4. **Process Information Safety**
- Added validation for `current_proc()` return values
- Safe process name retrieval with error handling
- Buffer initialization to prevent garbage data

### 5. **IOKit Object Safety**
- Enhanced validation for IORegistry objects
- Safe iterator usage patterns
- Proper retain/release handling

## Build Safety Features

### Compiler Flags Added
- `-fstack-protector-strong`: Stack overflow protection
- `-Wformat-security`: Format string security
- `-Wnull-dereference`: Null pointer warnings
- `-Warray-bounds`: Array bounds checking
- `-fno-delete-null-pointer-checks`: Preserve null checks

## Debugging Kernel Panics

### 1. **Enable Debug Logging**
Add to boot-args:
```
-liludbgall liludelay=1000 -liluoff -phantomdbg
```

### 2. **Safe Boot Testing**
```
revpatch=sbvmm
```
This skips VMM module initialization for testing.

### 3. **Common Panic Causes**
- **Bad Memory Access**: Usually in sysctl traversal
- **Stack Overflow**: From infinite loops in linked lists
- **Null Dereference**: Missing validation of kernel pointers
- **Double Free**: IOKit object management issues

### 4. **Recovery Steps**
1. Boot with `-phantomoff` to disable Phantom
2. Remove Phantom.kext from /Library/Extensions/
3. Run `sudo kextcache -i /` to rebuild cache
4. Reboot and test with fixed version

## Testing Checklist

Before deploying:
- [ ] Build with enhanced safety flags
- [ ] Test sysctl access: `sysctl kern.hv_vmm_present`
- [ ] Test process monitoring: `ps aux | grep League`
- [ ] Monitor kernel logs: `sudo dmesg | grep Phantom`
- [ ] Verify kext loading: `kextstat | grep Phantom`

## Boot Arguments for Testing

### Safe Mode
```
-v -s revpatch=sbvmm -phantomoff
```

### Debug Mode
```
-v -liludbgall liludelay=1000 -phantomdbg
```

### Selective Module Testing
```
# Test only VMM module
revpatch=sbslp,sbkmp,sbior

# Test only SecureLevel module  
revpatch=sbvmm,sbkmp,sbior

# Test only KextManager module
revpatch=sbvmm,sbslp,sbior

# Test only IORegistry module
revpatch=sbvmm,sbslp,sbkmp
```

## Error Codes Reference

### Common Kernel Return Codes
- `KERN_SUCCESS (0)`: Operation successful
- `KERN_FAILURE (5)`: General failure
- `KERN_INVALID_ARGUMENT (4)`: Invalid parameter
- `KERN_NO_ACCESS (8)`: Access denied
- `KERN_MEMORY_ERROR (10)`: Memory access error

### Phantom Module Errors
- Module initialization failure → Check logs for specific cause
- Sysctl reroute failure → Verify kernel symbol resolution
- Memory allocation failure → System resource exhaustion

## Monitoring Commands

### Real-time Kernel Messages
```bash
sudo dmesg -w | grep -E "(Phantom|PHTM|panic|fault)"
```

### Kext Status
```bash
kextstat | grep -E "(Phantom|Lilu)"
```

### System Integrity
```bash
sudo kextcache -system-prelinked-kernel
sudo kextcache -system-caches
```

## Recovery Procedures

### Emergency Kext Removal
```bash
# Boot to Recovery Mode or Safe Mode
sudo rm -rf /Library/Extensions/Phantom.kext
sudo rm -rf /Library/Extensions/Lilu.kext
sudo kextcache -clear-staging
sudo kextcache -i /
```

### Reset to Clean State
```bash
# Clear all kext caches
sudo rm -rf /Library/Caches/com.apple.kext.caches
sudo rm -rf /System/Library/Caches/com.apple.kext.caches
sudo kextcache -system-prelinked-kernel
sudo kextcache -system-caches
```

This guide should help prevent and diagnose kernel panics with Phantom.
