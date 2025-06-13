//
//  kern_kextmanager.cpp
//  Phantom
//
//  Created by RoyalGraphX on 6/3/25.
//

#include "kern_kextmanager.hpp"
#include <libkern/libkern.h>

// Constants
#define MAX_PROC_NAME_LEN 256

// Pointer to original declaration
static KMP::_OSKext_copyLoadedKextInfo_t original_OSKext_copyLoadedKextInfo = nullptr;

// Phantom's custom OSKext::copyLoadedKextInfo function, which cleanses the dict from 3rd party extensions
OSDictionary *phtm_OSKext_copyLoadedKextInfo(OSArray *kextIdentifiers, OSArray *bundlePaths) {

	// Enhanced safety checks for parameters
	proc_t currentProcess = current_proc();
	if (!currentProcess) {
		DBGLOG(MODULE_ERROR, "Failed to get current process in phtm_OSKext_copyLoadedKextInfo");
		// Fall back to original function if available
		if (original_OSKext_copyLoadedKextInfo) {
			return original_OSKext_copyLoadedKextInfo(kextIdentifiers, bundlePaths);
		}
		return nullptr;
	}
	
	pid_t procPid = proc_pid(currentProcess);
	char procName[MAX_PROC_NAME_LEN] = {0}; // Initialize buffer
		// Safely get process name (proc_name returns void)
	proc_name(procPid, procName, sizeof(procName));
	// Ensure null termination
	procName[sizeof(procName) - 1] = '\0';
	if (strlen(procName) == 0) {
		snprintf(procName, sizeof(procName), "unknown");
	}

	// Log the calling process information
	DBGLOG(MODULE_CLKI, "Process '%s' (PID: %d) called phtm_OSKext_copyLoadedKextInfo.", procName, procPid);

	if (!original_OSKext_copyLoadedKextInfo) {
		DBGLOG(MODULE_ERROR, "Original OSKext::copyLoadedKextInfo function is null for '%s' (PID: %d)", procName, procPid);
		return nullptr;
	}
	
	DBGLOG(MODULE_CLKI, "Calling original OSKext::copyLoadedKextInfo function for '%s' (PID: %d).", procName, procPid);
	OSDictionary *originalDict = original_OSKext_copyLoadedKextInfo(kextIdentifiers, bundlePaths);

	if (!originalDict) {
		DBGLOG(MODULE_CLKI, "Original function returned null dictionary for '%s' (PID: %d).", procName, procPid);
		return nullptr;
	}
	
	unsigned int originalCount = originalDict->getCount();
	DBGLOG(MODULE_CLKI, "Original function returned a dictionary with %u entries for '%s' (PID: %d).", originalCount, procName, procPid);

	// Create a new dictionary to store the filtered results.
	// OSDictionary::withCapacity returns an object with a retain count of 1.
	OSDictionary *filteredDict = OSDictionary::withCapacity(originalCount > 0 ? originalCount : 1); // Ensure at least 1
	if (!filteredDict) {
		DBGLOG(MODULE_CLKI, "Failed to allocate filteredDict for '%s' (PID: %d). Returning original (unmodified) dictionary.", procName, procPid);
		return originalDict;
	}

	unsigned int removedCount = 0;
	const char *filterSubstrings[] = {
		"org.Carnations",
		"org.acidanthera",
		"as.vit9696",
		"com.sn-labs"
	};

	unsigned int numFilters = sizeof(filterSubstrings) / sizeof(filterSubstrings[0]);
	OSCollectionIterator *iter = OSCollectionIterator::withCollection(originalDict);
	if (iter) {
		OSObject *keyObject;
		while ((keyObject = iter->getNextObject())) {
			OSString *bundleID = OSDynamicCast(OSString, keyObject); // Keys are bundle IDs (OSString)
			if (bundleID) {
				OSObject *value = originalDict->getObject(bundleID); // Get the associated kext info

				if (value) { // Should always be true if keyObject is valid
					const char *bundleIDCStr = bundleID->getCStringNoCopy();
					bool shouldFilterThisKext = false;
					const char *matchedFilter = nullptr;

					if (bundleIDCStr) { // Ensure C-string is valid
						for (unsigned int i = 0; i < numFilters; ++i) {
							// Use a safer string search to avoid ambiguity issues
							size_t bundleLen = strlen(bundleIDCStr);
							size_t filterLen = strlen(filterSubstrings[i]);
							bool found = false;
							
							if (bundleLen >= filterLen) {
								for (size_t j = 0; j <= bundleLen - filterLen; ++j) {
									if (strncmp(bundleIDCStr + j, filterSubstrings[i], filterLen) == 0) {
										found = true;
										break;
									}
								}
							}
							
							if (found) {
								shouldFilterThisKext = true;
								matchedFilter = filterSubstrings[i];
								break;
							}
						}
					}

					if (shouldFilterThisKext) {
						DBGLOG(MODULE_CLKI, "Filtering out kext: %s (filter match: '%s') for '%s' (PID: %d).", bundleIDCStr ? bundleIDCStr : "UnknownBundleID", matchedFilter, procName, procPid);
						removedCount++;
					} else {
						// This kext should be included. Add it to the filtered dictionary.
						// filteredDict->setObject retains both key and value.
						filteredDict->setObject(bundleID, value);
					}
				}
			}
		}
		iter->release(); // Release the iterator
	} else {
		DBGLOG(MODULE_CLKI, "Failed to create iterator for originalDict for '%s' (PID: %d). Returning original (unmodified) dictionary.", procName, procPid);
		filteredDict->release(); // Release the empty filteredDict we allocated
		return originalDict;     // Return the originalDict (already retained)
	}

	unsigned int filteredCount = filteredDict->getCount();
	DBGLOG(MODULE_CLKI, "Original dict had %u entries. Returning modified dict with %u entries (%u removed) for '%s' (PID: %d).", originalCount, filteredCount, removedCount, procName, procPid);
	originalDict->release();
	return filteredDict;
        
}

// Function to reroute CopyLoadedKextInfo
bool reRouteCopyLoadedKextInfo(KernelPatcher &patcher) {
    const char *mangledName = "__ZN6OSKext18copyLoadedKextInfoEP7OSArrayS1_"; // This has only been setup for Seq, and wil fail on other versions, probably
    
    // Create RouteRequest for the function  
    KernelPatcher::RouteRequest requests[] = {
        KernelPatcher::RouteRequest(mangledName, phtm_OSKext_copyLoadedKextInfo, original_OSKext_copyLoadedKextInfo)
    };
    
    // Attempt to route the function using template version
    if (patcher.routeMultiple(KernelPatcher::KernelID, requests, 1)) {
        if (original_OSKext_copyLoadedKextInfo) {
            DBGLOG(MODULE_RRKM, "Successfully routed %s.", mangledName);
            return true;
        } else {
            DBGLOG(MODULE_RRKM, "Failed to route %s - original function pointer is null", mangledName);
            return false;
        }
    } else {
        DBGLOG(MODULE_RRKM, "routeMultiple failed for %s", mangledName);
        return false;
    }
    
}

// Function for the KMP init routine
void KMP::init(KernelPatcher &Patcher) {
    DBGLOG(MODULE_KMP, "KMP::init() called. KMP module is starting.");    // Perform rerouting, as Patcher is available and known (hopefully by now, yes it is)
    if (!reRouteCopyLoadedKextInfo(Patcher)) {
		DBGLOG(MODULE_ERROR, "Failed to reroute copyLoadedKextInfo.");
		// Don't panic - just log error and continue
		return;
    } else {
        DBGLOG(MODULE_INFO, "copyLoadedKextInfo rerouted successfully.");
    }

}
