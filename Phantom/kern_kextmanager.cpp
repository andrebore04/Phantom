//
//  kern_kextmanager.cpp
//  Phantom
//
//  Created by RoyalGraphX on 6/3/25.
//

#include "kern_kextmanager.hpp"

// Pointer to original declaration
static KMP::_OSKext_copyLoadedKextInfo_t original_OSKext_copyLoadedKextInfo = nullptr;

// Phantom's custom OSKext::copyLoadedKextInfo function, which cleanses the dict from 3rd party extensions
OSDictionary *phtm_OSKext_copyLoadedKextInfo(OSArray *kextIdentifiers, OSArray *bundlePaths) {

	// Retrieve current process information
	proc_t currentProcess = current_proc();
	pid_t procPid = proc_pid(currentProcess);
	char procName[MAX_PROC_NAME_LEN];
	proc_name(procPid, procName, sizeof(procName));

	// Log the calling process information
	DBGLOG(MODULE_CLKI, "Process '%s' (PID: %d) called phtm_OSKext_copyLoadedKextInfo.", procName, procPid);

	if (original_OSKext_copyLoadedKextInfo) {
		DBGLOG(MODULE_CLKI, "Calling original OSKext::copyLoadedKextInfo function for '%s' (PID: %d).", procName, procPid);
		OSDictionary *originalDict = original_OSKext_copyLoadedKextInfo(kextIdentifiers, bundlePaths);

		if (originalDict) {
			unsigned int originalCount = originalDict->getCount();
			DBGLOG(MODULE_CLKI, "Original function returned a dictionary with %u entries for '%s' (PID: %d).", originalCount, procName, procPid);

			// Create a new dictionary to store the filtered results.
			// OSDictionary::withCapacity returns an object with a retain count of 1.
			OSDictionary *filteredDict = OSDictionary::withCapacity(originalCount > 0 ? originalCount -1 : 0); // Estimate capacity

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
									if (strstr(bundleIDCStr, filterSubstrings[i]) != nullptr) {
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
			} // we couldn't modify the dict, something went wrong, return the og dict

			unsigned int filteredCount = filteredDict->getCount();
			DBGLOG(MODULE_CLKI, "Original dict had %u entries. Returning modified dict with %u entries (%u removed) for '%s' (PID: %d).", originalCount, filteredCount, removedCount, procName, procPid);
			originalDict->release();
			return filteredDict;

		} else { // originalDict was nullptr from the call
			DBGLOG(MODULE_CLKI, "Original function returned nullptr for '%s' (PID: %d).", procName, procPid);
			return nullptr;
		}
	} else { // original_OSKext_copyLoadedKextInfo function pointer was null (hooking failed badly)
		DBGLOG(MODULE_CLKI, "Original OSKext::copyLoadedKextInfo is null for '%s' (PID: %d). Returning empty dictionary.", procName, procPid);
		// Fallback: return a new, empty, retained dictionary
		return OSDictionary::withCapacity(0);
	}
        
}

// Function to reroute CopyLoadedKextInfo
bool reRouteCopyLoadedKextInfo(KernelPatcher &patcher) {
    
    const char *mangledName = "__ZN6OSKext18copyLoadedKextInfoEP7OSArrayS1_"; // This has only been setup for Seq, and wil fail on other versions, probably
    mach_vm_address_t functionAddress = patcher.solveSymbol(KernelPatcher::KernelID, mangledName);

    if (functionAddress) {
        DBGLOG(MODULE_RRKM, "Resolved %s at 0x%llx", mangledName, functionAddress);

        // Attempt to route the function
        original_OSKext_copyLoadedKextInfo = reinterpret_cast<KMP::_OSKext_copyLoadedKextInfo_t>(
            patcher.routeFunction(
                functionAddress,
                reinterpret_cast<mach_vm_address_t>(phtm_OSKext_copyLoadedKextInfo),
                true // Create a trampoline to call the original, this is needed to sanitize the OG content
            )
        );

        if (patcher.getError() == KernelPatcher::Error::NoError && original_OSKext_copyLoadedKextInfo) {
            DBGLOG(MODULE_RRKM, "Successfully routed %s.", mangledName);
            return true;
        } else {
			DBGLOG(MODULE_RRKM, "Failed to route %s. Lilu error: %d. Original ptr: %p", mangledName, patcher.getError(), original_OSKext_copyLoadedKextInfo);
            original_OSKext_copyLoadedKextInfo = nullptr;
            patcher.clearError();
            return false;
        }
    } else {
        DBGLOG(MODULE_RRKM, "Failed to resolve symbol %s. Lilu error: %d", mangledName, patcher.getError());
        patcher.clearError();
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
