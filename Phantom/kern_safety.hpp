//
//  kern_safety.hpp
//  Phantom
//
//  Safety macros and validation functions
//

#ifndef kern_safety_hpp
#define kern_safety_hpp

#include "kern_start.hpp"

// Safe kernel address validation
static inline bool isValidKernelAddress(mach_vm_address_t addr) {
    return (addr >= 0xffffff8000000000ULL && addr <= 0xfffffffffffffffULL && (addr & 0x7) == 0);
}

// Safe string length check with maximum limit
static inline bool isSafeString(const char *str, size_t maxLen = 256) {
    if (!str) return false;
    return strnlen(str, maxLen) < maxLen;
}

// Safe sysctl node validation
static inline bool isValidSysctlNode(sysctl_oid *node) {
    return (node != nullptr && node->oid_name != nullptr && isSafeString(node->oid_name, 64));
}

// Enhanced logging for safety issues
#define SAFETY_LOG(fmt, ...) DBGLOG("SAFETY", fmt, ##__VA_ARGS__)

// Safe memory write wrapper
static inline kern_return_t safeKernelWrite(KernelPatcher &patcher, bool enable) {
    kern_return_t result = MachInfo::setKernelWriting(enable, patcher.kernelWriteLock);
    if (result != KERN_SUCCESS) {
        SAFETY_LOG("Kernel write %s failed with error %d", enable ? "enable" : "disable", result);
    }
    return result;
}

#endif /* kern_safety_hpp */
