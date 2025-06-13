//
//  kern_vmm.cpp
//  Phantom
//
//  Created by RoyalGraphX on 6/3/25.
//

#include "kern_vmm.hpp"
#include <Headers/kern_mach.hpp>
#include <Headers/kern_patcher.hpp>

// static integer to keep track of initial and post reroute presence.
int VMM::hvVmmPresent = 0;
size_t hvVmmIntSize = sizeof(VMM::hvVmmPresent);
sysctl_handler_t VMM::originalHvVmmHandler = nullptr;

/**
 * @brief Defines the list of processes to filter for the VMM module.
 * If a process calling kern.hv_vmm_present is in this list, the call will return 1.
 * For all other processes, the call will return 0.
 * The pid is not used in this check, so it can be left as 0.
 */
const PHTM::DetectedProcess VMM::filteredProcs[] = {
	{"SoftwareUpdateNo", -1},
    {"softwareupdated", -1},
    {"com.apple.Mobile", -1},
    {"osinstallersetup", -1}
};

// Phantom's custom sysctl VMM present function
int phtm_sysctl_vmm_present(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
    
    // Enhanced safety checks for parameters
    if (!oidp || !req) {
        DBGLOG(MODULE_ERROR, "Invalid parameters passed to phtm_sysctl_vmm_present");
        return EINVAL;
    }
    
    // Retrieve the current process information
    proc_t currentProcess = current_proc();
    if (!currentProcess) {
        DBGLOG(MODULE_ERROR, "Failed to get current process in phtm_sysctl_vmm_present");
        return EINVAL;
    }
      pid_t procPid = proc_pid(currentProcess);
    char procName[MAX_PROC_NAME_LEN] = {0}; // Initialize buffer
    
    // Safely get process name (proc_name returns void, not int)
    proc_name(procPid, procName, sizeof(procName));
    // Ensure null termination
    procName[MAX_PROC_NAME_LEN - 1] = '\0';
    
    // Default to 0 (VMM not present). This will be the value for any process NOT in our list.
    int value_to_return = 0;
    bool isFiltered = false;

    // Determine the number of processes in our filter list
    const size_t num_filtered = sizeof(VMM::filteredProcs) / sizeof(VMM::filteredProcs[0]);

    // Loop through the filteredProcs list to check for a match
    for (size_t i = 0; i < num_filtered; ++i) {
        // Use strcmp to compare the current process name with the name in our list
        if (strcmp(procName, VMM::filteredProcs[i].name) == 0) {
            // Match found! Set the return value to 1 (VMM is present).
            value_to_return = 1;
            isFiltered = true;
            break; // Exit the loop since we found our match
        }
    }

    // Log the action for debugging purposes
    if (isFiltered) {
        DBGLOG(MODULE_CVMM, "Process '%s' (PID: %d) is on the filter list. Reporting hv_vmm_present as %d.", procName, procPid, value_to_return);
    } else {
        DBGLOG(MODULE_CVMM, "Process '%s' (PID: %d) is NOT on the filter list. Reporting hv_vmm_present as %d.", procName, procPid, value_to_return);
    }
    
    // Use the kernel macro to properly return the value to the calling process, depending on our context
    return SYSCTL_OUT(req, &value_to_return, sizeof(value_to_return));
}

// Function to reroute kern.hv_vmm_present function to our own custom one
bool reRouteHvVmm(KernelPatcher &patcher) {    // Ensure that sysctlChildrenAddress exists before continuing
    if (!PHTM::gSysctlChildrenAddr) {
		DBGLOG(MODULE_ERROR, "Failed to resolve _sysctl__children passed to function reRouteHvVmm.");
		// Don't panic - just return false to indicate failure
        return false;
	} else {
		DBGLOG(MODULE_RRHVM, "Got address 0x%llx for _sysctl__children passed to function reRouteHvVmm.", PHTM::gSysctlChildrenAddr);
	}    // Validate the address is in kernel space
    if (PHTM::gSysctlChildrenAddr < 0xffffff8000000000ULL) {
        DBGLOG(MODULE_RRHVM, "Invalid _sysctl__children address: 0x%llx", PHTM::gSysctlChildrenAddr);
        return false;
    }

    // Cast the address to sysctl_oid_list* with additional validation
    sysctl_oid_list *sysctlChildren = reinterpret_cast<sysctl_oid_list *>(PHTM::gSysctlChildrenAddr);
    
    if (!sysctlChildren) {
        DBGLOG(MODULE_RRHVM, "sysctlChildren is null after casting");
        return false;
    }
    
    // traverse the sysctl tree to locate 'kern'
    sysctl_oid *kernNode = nullptr;
    int traversalCount = 0;
    const int MAX_TRAVERSAL = 1000; // Prevent infinite loops
    
    SLIST_FOREACH(kernNode, sysctlChildren, oid_link) {
        // Prevent infinite loops
        if (++traversalCount > MAX_TRAVERSAL) {
            DBGLOG(MODULE_RRHVM, "Traversal limit exceeded, aborting");
            return false;
        }
        
        // Enhanced safety checks
        if (!kernNode) {
            DBGLOG(MODULE_RRHVM, "Encountered NULL node during traversal, skipping");
            continue;
        }
        
        if (!kernNode->oid_name) {
            DBGLOG(MODULE_RRHVM, "Encountered node with NULL name, skipping");
            continue;
        }
        
        // Safe string comparison with length limit
        if (strnlen(kernNode->oid_name, 32) < 32 && strcmp(kernNode->oid_name, "kern") == 0) {
            DBGLOG(MODULE_RRHVM, "Found 'kern' node.");
            break;
        }
    }    // check if kern node was found
    if (!kernNode) {
        DBGLOG(MODULE_RRHVM, "Failed to locate 'kern' node in sysctl tree.");
        return false;
    }
    
    // Validate kernNode before accessing its members
    if (!kernNode->oid_arg1) {
        DBGLOG(MODULE_RRHVM, "kern node has null oid_arg1");
        return false;
    }
    
    // traverse 'kern' to find 'hv_vmm_present'
    sysctl_oid_list *kernChildren = reinterpret_cast<sysctl_oid_list *>(kernNode->oid_arg1);
    if (!kernChildren) {
        DBGLOG(MODULE_RRHVM, "kern node has no children");
        return false;
    }
    
    sysctl_oid *vmmNode = nullptr;
    traversalCount = 0; // Reset counter for kern children
    
    SLIST_FOREACH(vmmNode, kernChildren, oid_link) {
        // Prevent infinite loops in kern children
        if (++traversalCount > MAX_TRAVERSAL) {
            DBGLOG(MODULE_RRHVM, "Kern children traversal limit exceeded, aborting");
            return false;
        }
        
        // Enhanced safety checks for vmm node
        if (!vmmNode) {
            DBGLOG(MODULE_RRHVM, "Encountered NULL node in kern children, skipping");
            continue;
        }
        
        if (!vmmNode->oid_name) {
            DBGLOG(MODULE_RRHVM, "Encountered node with NULL name in kern children, skipping");
            continue;
        }
        
        // Safe string comparison with length limit
        if (strnlen(vmmNode->oid_name, 32) < 32 && strcmp(vmmNode->oid_name, "hv_vmm_present") == 0) {
            DBGLOG(MODULE_RRHVM, "Found 'hv_vmm_present' node.");
            break;
        }
    }

    // check if the vmm present entry was found
    if (!vmmNode) {
        DBGLOG(MODULE_RRHVM, "Failed to locate 'hv_vmm_present' sysctl entry.");
        return false;
    }

	// Check if the found vmmNode's handler is NULL, which might be unexpected.
    if (vmmNode->oid_handler == nullptr) {
        DBGLOG(MODULE_RRHVM, "Failed to save original 'hv_vmm_present' sysctl handler: The existing handler was NULL.");
        return false; // Return false as this is considered a failure condition.
    }    // save the original handler in the global variable
	VMM::originalHvVmmHandler = vmmNode->oid_handler;
    DBGLOG(MODULE_RRHVM, "Successfully saved original 'hv_vmm_present' sysctl handler.");
    
    // Safely enable kernel writing with error checking (using correct Lilu API)
    kern_return_t writeResult = MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock);
    if (writeResult != KERN_SUCCESS) {
        DBGLOG(MODULE_ERROR, "Failed to enable kernel writing (error: %d). Aborting hv_vmm_present reroute.", writeResult);
        return false;
    }
    
    // reroute the handler to our custom function
    vmmNode->oid_handler = phtm_sysctl_vmm_present;
    
    // Safely disable kernel writing
    writeResult = MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    if (writeResult != KERN_SUCCESS) {
        DBGLOG(MODULE_WARN, "Warning: Failed to disable kernel writing (error: %d). System may be unstable.", writeResult);
        // Don't return false here as the reroute was successful
    }
    
    DBGLOG(MODULE_RRHVM, "Successfully rerouted 'hv_vmm_present' sysctl handler.");
    return true;

}

// Function for the VMM init routine
void VMM::init(KernelPatcher &Patcher) {

	// Register a request to reroute to our custom function
    DBGLOG(MODULE_VMM, "VMM::init() called. VMM module is starting.");
		if (!PHTM::gSysctlChildrenAddr) {
        DBGLOG(MODULE_ERROR, "PHTM::gSysctlChildrenAddr is not set. Cannot perform VMM rerouting.");
		// Don't panic - just return to prevent system crash
        return;
    }    // Perform rerouting, as Patcher is available and gSysctlChildrenAddr is known (hopefully by now, yes it is)
    if (!reRouteHvVmm(Patcher)) {
		DBGLOG(MODULE_ERROR, "Failed to reroute kern.hv_vmm_present.");
		// Don't panic - just log error and continue
		return;
    } else {
        DBGLOG(MODULE_INFO, "kern.hv_vmm_present rerouted successfully.");
    }

}
