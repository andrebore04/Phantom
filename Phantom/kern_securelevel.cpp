//
//  kern_securelevel.cpp
//  Phantom
//
//  Created by RoyalGraphX on 6/5/25.
//

#include "kern_securelevel.hpp"
#include <Headers/kern_mach.hpp>
#include <Headers/kern_patcher.hpp>

// Pointer to original declaration
sysctl_handler_t SLP::originalSecureLevelHandler = nullptr;

// Phantom's custom sysctl securelevel function, this one returns 1 always, to say yes we're enabled
int phtm_sysctl_securelevel(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {    // Enhanced safety checks for parameters
    if (!oidp || !req) {
        DBGLOG(MODULE_ERROR, "Invalid parameters passed to phtm_sysctl_securelevel");
        return EINVAL;
    }
    
    proc_t currentProcess = current_proc();
    if (!currentProcess) {
        DBGLOG(MODULE_ERROR, "Failed to get current process in phtm_sysctl_securelevel");
        return EINVAL;
    }
    
    pid_t procPid = proc_pid(currentProcess);
    char procName[MAX_PROC_NAME_LEN] = {0}; // Initialize buffer
    
    // Safely get process name (proc_name returns void, not int)
    proc_name(procPid, procName, sizeof(procName));
    // Ensure null termination
    procName[MAX_PROC_NAME_LEN - 1] = '\0';
    
    int spoofed_securelevel = 1;
    
    DBGLOG(MODULE_KSL, "Process '%s' (PID: %d) accessed kern.securelevel. Spoofing value to %d.", procName, procPid, spoofed_securelevel);
    return SYSCTL_OUT(req, &spoofed_securelevel, sizeof(spoofed_securelevel));
    
}

// Function to reroute kern.securelevel to our custom one
bool reRouteSecureLevel(KernelPatcher &patcher) {
        
    // ensure that sysctlChildrenAddress exists before continuing
    if (!PHTM::gSysctlChildrenAddr) {
		DBGLOG(MODULE_ERROR, "Failed to resolve _sysctl__children passed to function reRouteSecureLevel.");
        return false;
	} else {
		DBGLOG(MODULE_RRSL, "Got address 0x%llx for _sysctl__children passed to function reRouteSecureLevel.", PHTM::gSysctlChildrenAddr);
	}    // Validate the address is in kernel space
    if (PHTM::gSysctlChildrenAddr < 0xffffff8000000000ULL) {
        DBGLOG(MODULE_RRSL, "Invalid _sysctl__children address: 0x%llx", PHTM::gSysctlChildrenAddr);
        return false;
    }
    
    sysctl_oid_list *sysctlChildren = reinterpret_cast<sysctl_oid_list *>(PHTM::gSysctlChildrenAddr);
    
    // Additional safety check for the list itself
    if (!sysctlChildren) {
        DBGLOG(MODULE_RRSL, "sysctlChildren is null after casting");
        return false;
    }
    
    sysctl_oid *kernNode = nullptr;
    int traversalCount = 0;
    const int MAX_TRAVERSAL = 1000; // Prevent infinite loops
    
    SLIST_FOREACH(kernNode, sysctlChildren, oid_link) {
        // Prevent infinite loops
        if (++traversalCount > MAX_TRAVERSAL) {
            DBGLOG(MODULE_RRSL, "Traversal limit exceeded, aborting");
            return false;
        }
        
        // Enhanced safety checks
        if (!kernNode) {
            DBGLOG(MODULE_RRSL, "Encountered NULL node during traversal, skipping");
            continue;
        }
        
        if (!kernNode->oid_name) {
            DBGLOG(MODULE_RRSL, "Encountered node with NULL name, skipping");
            continue;
        }
        
        // Safe string comparison with length limit
        if (strnlen(kernNode->oid_name, 32) < 32 && strcmp(kernNode->oid_name, "kern") == 0) {
            DBGLOG(MODULE_RRSL, "Found 'kern' node for securelevel.");
            break;
        }
    }    if (!kernNode) {
        DBGLOG(MODULE_RRSL, "Failed to locate 'kern' node in sysctl tree for securelevel.");
        return false;
    }
    
    // Validate kernNode before accessing its members
    if (!kernNode->oid_arg1) {
        DBGLOG(MODULE_RRSL, "kern node has null oid_arg1");
        return false;
    }
    
    sysctl_oid_list *kernChildren = reinterpret_cast<sysctl_oid_list *>(kernNode->oid_arg1);
    if (!kernChildren) {
        DBGLOG(MODULE_RRSL, "kern node has no children");
        return false;
    }
    
    sysctl_oid *securelevelNode = nullptr;
    traversalCount = 0; // Reset counter for kern children
    
    SLIST_FOREACH(securelevelNode, kernChildren, oid_link) {
        // Prevent infinite loops in kern children
        if (++traversalCount > MAX_TRAVERSAL) {
            DBGLOG(MODULE_RRSL, "Kern children traversal limit exceeded, aborting");
            return false;
        }
        
        // Enhanced safety checks for securelevel node
        if (!securelevelNode) {
            DBGLOG(MODULE_RRSL, "Encountered NULL node in kern children, skipping");
            continue;
        }
        
        if (!securelevelNode->oid_name) {
            DBGLOG(MODULE_RRSL, "Encountered node with NULL name in kern children, skipping");
            continue;
        }
        
        // Safe string comparison with length limit
        if (strnlen(securelevelNode->oid_name, 32) < 32 && strcmp(securelevelNode->oid_name, "securelevel") == 0) {
            DBGLOG(MODULE_RRSL, "Found 'securelevel' node.");
            break;
        }
    }if (!securelevelNode) {
        DBGLOG(MODULE_RRSL, "Failed to locate 'securelevel' sysctl entry.");
        return false;
    }

    // Check if the found securelevelNode's handler is NULL, which might be unexpected.
    if (securelevelNode->oid_handler == nullptr) {
        DBGLOG(MODULE_RRSL, "Failed to save original 'securelevel' sysctl handler: The existing handler was NULL.");
        return false; // Return false as this is considered a failure condition.
    }	SLP::originalSecureLevelHandler = securelevelNode->oid_handler;
    DBGLOG(MODULE_RRSL, "Successfully saved original 'securelevel' sysctl handler.");
	
	// Safely enable kernel writing with error checking (using correct Lilu API)
    kern_return_t writeResult = MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock);
    if (writeResult != KERN_SUCCESS) {
        DBGLOG(MODULE_ERROR, "Failed to enable kernel writing (error: %d). Aborting securelevel reroute.", writeResult);
        return false;
    }
    
    // Reroute the handler to our custom function
    securelevelNode->oid_handler = phtm_sysctl_securelevel;
    
    // Safely disable kernel writing
    writeResult = MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    if (writeResult != KERN_SUCCESS) {
        DBGLOG(MODULE_WARN, "Warning: Failed to disable kernel writing (error: %d). System may be unstable.", writeResult);
        // Don't return false here as the reroute was successful
    }
    
    DBGLOG(MODULE_RRSL, "Successfully rerouted 'securelevel' sysctl handler.");
    return true;
	
}

// Function for the KMP init routine
void SLP::init(KernelPatcher &Patcher) {
    DBGLOG(MODULE_SLP, "SLP::init() called. SLP module is starting.");
	
	if (!PHTM::gSysctlChildrenAddr) {
        DBGLOG(MODULE_ERROR, "PHTM::gSysctlChildrenAddr is not set. Cannot perform SLP rerouting.");
        return;
    }    // Perform rerouting, as Patcher is available and known (hopefully by now, yes it is)
    if (!reRouteSecureLevel(Patcher)) {
		DBGLOG(MODULE_ERROR, "Failed to reroute kern.securelevel.");
		// Don't panic - just log error and continue
		return;
    } else {
        DBGLOG(MODULE_INFO, "kern.securelevel rerouted successfully.");
    }

}
