//
//  kern_start.cpp
//  Phantom
//
//  Created by RoyalGraphX on 10/12/24.
//

#include "kern_start.hpp"
#include "kern_vmm.hpp"
#include "kern_kextmanager.hpp"
#include "kern_securelevel.hpp"
// #include "kern_csr.hpp"
#include "kern_ioreg.hpp"

static PHTM phtmInstance;
PHTM *PHTM::callbackPHTM;

// Definition for the global _sysctl__children address
mach_vm_address_t PHTM::gSysctlChildrenAddr = 0;

// To only be modified by CarnationsInternal, to display various Internal logs and headers
const bool PHTM::IS_INTERNAL = false; // MUST CHANCE THIS TO FALSE BEFORE CREATING COMMITS

// Function to get _sysctl__children memory address
mach_vm_address_t PHTM::sysctlChildrenAddr(KernelPatcher &patcher) {
    // Resolve the _sysctl__children symbol with the given patcher
    mach_vm_address_t resolvedAddress = patcher.solveSymbol(KernelPatcher::KernelID, "_sysctl__children");
    
    // Check if the address was successfully resolved, else return 0
    if (resolvedAddress) {
        DBGLOG(MODULE_SYSCA, "Resolved _sysctl__children at address: 0x%llx", resolvedAddress);
        
        // Enhanced validation for kernel space addresses
        if (resolvedAddress < 0xffffff8000000000ULL || resolvedAddress > 0xfffffffffffffffULL) {
            DBGLOG(MODULE_SYSCA, "Resolved address appears invalid (out of kernel range): 0x%llx", resolvedAddress);
            return 0;
        }
        
        // Additional validation - check if address is aligned
        if (resolvedAddress & 0x7) {
            DBGLOG(MODULE_SYSCA, "Resolved address is not 8-byte aligned: 0x%llx", resolvedAddress);
            return 0;
        }

        // Completely disable debug iteration - this can cause kernel panics
        #if 0
        sysctl_oid_list *sysctlChildrenList = reinterpret_cast<sysctl_oid_list *>(resolvedAddress);
        DBGLOG(MODULE_SYSCA, "Sysctl children list at address: 0x%llx", reinterpret_cast<mach_vm_address_t>(sysctlChildrenList));
        sysctl_oid *oid;
        SLIST_FOREACH(oid, sysctlChildrenList, oid_link) {
            // Add safety checks to prevent crashes
            if (oid && oid->oid_name) {
                DBGLOG(MODULE_SYSCA, "OID Name: %s, OID Number: %d", oid->oid_name, oid->oid_number);
            } else {
                DBGLOG(MODULE_SYSCA, "Found null OID or OID name, stopping iteration");
                break;
            }
        }
        #endif
        
        return resolvedAddress;
    } else {
        KernelPatcher::Error err = patcher.getError();
        DBGLOG(MODULE_SYSCA, "Failed to resolve _sysctl__children. (Lilu returned: %d)", err);
        patcher.clearError();
        return 0;
    }
	
}

// Callback function to solve for and store _sysctl__children address
void PHTM::solveSysCtlChildrenAddr(void *user __unused, KernelPatcher &Patcher) {
    DBGLOG(MODULE_SSYSCTL, "PHTM::solveSysCtlChildrenAddr called successfully. Attempting to resolve and store _sysctl__children address.");
	
    PHTM::gSysctlChildrenAddr = PHTM::sysctlChildrenAddr(Patcher);
    
    if (!PHTM::gSysctlChildrenAddr) {
        DBGLOG(MODULE_SSYSCTL, "Failed to resolve _sysctl__children address. Skipping further setup.");
        
        return;
    }

    DBGLOG(MODULE_SSYSCTL, "Successfully resolved and stored _sysctl__children address: 0x%llx", PHTM::gSysctlChildrenAddr);
	
    // Now, initialize dependent modules, passing the KernelPatcher instance
	
    // Conditional VMM Module Initialization
    bool initializeVMM = true;
    char revpatchValue[256] = {0};    if (PE_parse_boot_argn("revpatch", revpatchValue, sizeof(revpatchValue))) {
        // The 'revpatch' boot-arg exists. Now check for "sbvmm".
        // strstr() will return a non-NULL pointer if "sbvmm" is found anywhere in the value string.
        if (strstr(revpatchValue, "sbvmm") != nullptr) {
            // "sbvmm" was found, so we will NOT initialize the VMM module.
            initializeVMM = false;
            DBGLOG(MODULE_INIT, "Found 'sbvmm' in revpatch boot-arg, skipping VMM module initialization.");
        }
    }
      if (initializeVMM) {
        DBGLOG(MODULE_INIT, "Initializing VMM module.");
        VMM::init(Patcher);
    }
    // End of Conditional VMM Initialization
	
    DBGLOG(MODULE_INIT, "Initializing KMP module.");
    KMP::init(Patcher);
	
    DBGLOG(MODULE_INIT, "Initializing SLP module.");
    SLP::init(Patcher);
	
	DBGLOG(MODULE_INIT, "Initializing IOR module.");
    IOR::init(Patcher);
	
	// DBGLOG(MODULE_INIT, "Initializing CSR module.");
	// CSR::init(Patcher);
	
    DBGLOG(MODULE_SSYSCTL, "Finished all reroute attempts.");
}

// Main PHTM Routine function
void PHTM::init() {
	
	// EXPRESSED PERMISSIONS Header BEGIN
	// DO NOT MODIFY. NO EXPRESSED PERMISSION IS GIVEN BY CARNATIONS BOTANICA TO DO SO. NO EXCEPTIONS.
	// DO NOT MODIFY THE MARKED HEADER SECTION FOR EXPRESSED PERMISSIONS.
	
	// Do NOT include Phantom in your EFIs and share them.
	// Do NOT redistribute Phantom in binary form.
	// In any way, shape, or form. All users must source Phantom on their own, and apply the kernel extension with ProperTree manually.
	// If you are found to contain Phantom within your repository which provides an OpenCore EFI, you are subject to a DMCA request for violating
	// this incredibly clear warning and demonstration that we do not condone bypassing these efforts to limit the kind of users who can use Phantom.
	
	// For contributors, you can freely fork and work on improving Phantom without worry and with CI enabled. This header is still not
	// allowed for modification, even on forks with CI enabled, which allow for circumventing this header by providing an alternative
	// binary without said header. Again, your repository *will* be DMCA'd. You have been expressed no permission multiple times, and
	// we are allowed to protect our work as we see fit. Thank you for understanding and agreeing to these terms if you are using Phantom in any way,
	// shape or form, as per assumptions that you, are not Carnations Botanica, or CarnationsInternal. These clauses apply ontop of the LICENSE seen.
	// EXPRESSED PERMISSIONS Header END
    
    // Start off the routine
    callbackPHTM = this;
    int major = getKernelVersion();
    int minor = getKernelMinorVersion();
    const char* phtmVersionNumber = PHTM_VERSION;
    DBGLOG(MODULE_INIT, "Hello World from Phantom!");
    DBGLOG(MODULE_INFO, "Current Build Version running: %s", phtmVersionNumber);
    DBGLOG(MODULE_INFO, "Copyright © 2024, 2025 Carnations Botanica. All rights reserved.");
    if (major > 0) {
        DBGLOG(MODULE_INFO, "Current Darwin Kernel version: %d.%d", major, minor);
    } else {
        DBGLOG(MODULE_ERROR, "WARNING: Failed to retrieve Darwin Kernel version.");
    }
    
    // Internal Header BEGIN
    if (PHTM::IS_INTERNAL) {
        DBGLOG(MODULE_WARN, "");
        DBGLOG(MODULE_WARN, "==================================================================");
		DBGLOG(MODULE_WARN, "This build of %s is for CarnationsInternal usage only!", MODULE_LONG);
        DBGLOG(MODULE_WARN, "If you received a copy of this binary as a tester, DO NOT SHARE.");
        DBGLOG(MODULE_WARN, "=================================================================");
        DBGLOG(MODULE_WARN, "");
    }
    // Internal Header END
	
    // Register the main sysctl children address resolver
    DBGLOG(MODULE_INIT, "Registering PHTM::solveSysCtlChildrenAddr with onPatcherLoadForce.");
    lilu.onPatcherLoadForce(&PHTM::solveSysCtlChildrenAddr);

}

// We use phtmState to determine PHTM behaviour
void PHTM::deinit() {
    
    DBGLOG(MODULE_ERROR, "This kernel extension cannot be disabled this way!");
    SYSLOG(MODULE_ERROR, "This kernel extension cannot be disabled this way!");
    
}

const char *bootargOff[] {
    "-phtmoff"
};

const char *bootargDebug[] {
    "-phtmdbg"
};

const char *bootargBeta[] {
    "-phtmbeta"
};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal |
    LiluAPI::AllowSafeMode |
    LiluAPI::AllowInstallerRecovery,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),    bootargBeta,
    arrsize(bootargBeta),
	KernelVersion::Monterey, // Narrowed range for initial testing - was HighSierra
	KernelVersion::Sequoia,
    []() {
        
        // Start the main PHTM routine
        phtmInstance.init();
        
    }
};
