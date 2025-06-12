//
//  kern_vmm.cpp
//  Phantom
//
//  Created by RoyalGraphX on 6/3/25.
//

#include "kern_vmm.hpp"

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
    
    // Retrieve the current process information
    proc_t currentProcess = current_proc();
    pid_t procPid = proc_pid(currentProcess);
    char procName[MAX_PROC_NAME_LEN] = {0};
    proc_name(procPid, procName, sizeof(procName));
    
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
	}

    // Case the address to sysctl_oid_list*
    sysctl_oid_list *sysctlChildren = reinterpret_cast<sysctl_oid_list *>(PHTM::gSysctlChildrenAddr);    // traverse the sysctl tree to locate 'kern'
    sysctl_oid *kernNode = nullptr;
    SLIST_FOREACH(kernNode, sysctlChildren, oid_link) {
        // Add safety check for NULL node and name
        if (!kernNode || !kernNode->oid_name) {
            DBGLOG(MODULE_RRHVM, "Encountered NULL node or name during traversal, skipping");
            continue;
        }
        if (strcmp(kernNode->oid_name, "kern") == 0) {
            DBGLOG(MODULE_RRHVM, "Found 'kern' node.");
            break;
        }
    }

    // check if kern node was found
    if (!kernNode) {
        DBGLOG(MODULE_RRHVM, "Failed to locate 'kern' node in sysctl tree.");
        return false;
    }    // traverse 'kern' to find 'hv_vmm_present'
    sysctl_oid_list *kernChildren = reinterpret_cast<sysctl_oid_list *>(kernNode->oid_arg1);
    if (!kernChildren) {
        DBGLOG(MODULE_RRHVM, "kern node has no children");
        return false;
    }
    
    sysctl_oid *vmmNode = nullptr;
    SLIST_FOREACH(vmmNode, kernChildren, oid_link) {
        // Add safety check for NULL node and name
        if (!vmmNode || !vmmNode->oid_name) {
            DBGLOG(MODULE_RRHVM, "Encountered NULL node or name in kern children, skipping");
            continue;
        }
        if (strcmp(vmmNode->oid_name, "hv_vmm_present") == 0) {
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
    }
	
    // save the original handler in the global variable
	VMM::originalHvVmmHandler = vmmNode->oid_handler;
    DBGLOG(MODULE_RRHVM, "Successfully saved original 'hv_vmm_present' sysctl handler.");
    
    // ensure kernel r/w access
    PANIC_COND(MachInfo::setKernelWriting(true, patcher.kernelWriteLock) != KERN_SUCCESS, MODULE_SHORT, "Failed to enable God mode. (Kernel R/W)");
    
    // reroute the handler to our custom function
    vmmNode->oid_handler = phtm_sysctl_vmm_present;
    MachInfo::setKernelWriting(false, patcher.kernelWriteLock);
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
