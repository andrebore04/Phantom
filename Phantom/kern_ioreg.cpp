//
//  kern_ioreg.cpp
//  Phantom
//
//  Created by RoyalGraphX on 6/7/25.
//

#include "kern_ioreg.hpp"

// Static pointers to hold the original function addresses
static IOR::_IORegistryEntry_getProperty_t original_IORegistryEntry_getProperty_os_symbol = nullptr;
static IOR::_IORegistryEntry_getProperty_cstring_t original_IORegistryEntry_getProperty_cstring = nullptr;
static IOR::_IORegistryEntry_getName_t original_IORegistryEntry_getName = nullptr;
static IOR::_IOIterator_getNextObject_t original_IOIterator_getNextObject = nullptr;
static IOR::_IOService_getMatchingServices_t original_IOService_getMatchingServices = nullptr;
static IOR::_IOService_getMatchingService_t original_IOService_getMatchingService = nullptr;

// Module-specific Filtered Process List
const PHTM::DetectedProcess IOR::filteredProcs[] = {
    { "Terminal", 0 },
    { "ioreg", 0 },
    { "LeagueClient", 0 },
    { "LeagueofLegends", 0 },
    { "LeagueClientUx H", 0 },
    { "RiotClientServic", 0 }
};

// List of IORegistry class names to hide from the filtered processes.
const char *IOR::filteredClasses[] = {
	"AppleVirtIONetwork",
	"AppleVirtIOPCITransport",
    "AppleVirtIOBlockStorageDevice",
};

// Generic isProcFiltered Helper Function
static bool isProcFiltered(const char *procName) {
    if (!procName || strnlen(procName, MAX_PROC_NAME_LEN) >= MAX_PROC_NAME_LEN) {
        return false;
    }
    const size_t num_filtered = sizeof(IOR::filteredProcs) / sizeof(IOR::filteredProcs[0]);
    for (size_t i = 0; i < num_filtered; ++i) {
        if (IOR::filteredProcs[i].name && strcmp(procName, IOR::filteredProcs[i].name) == 0) {
            return true;
        }
    }
    return false;
}

// Helper to check if a class name should be filtered
static bool isClassFiltered(const char *className) {
    if (!className || strnlen(className, 256) >= 256) {
        return false;
    }
    const size_t num_filtered = sizeof(IOR::filteredClasses) / sizeof(IOR::filteredClasses[0]);
    for (size_t i = 0; i < num_filtered; ++i) {
        if (strcmp(className, IOR::filteredClasses[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Sanitizes the results of service matching requests.
OSIterator *phtm_IOService_getMatchingServices(OSDictionary *matching) {
    proc_t p = current_proc();
    pid_t pid = proc_pid(p);
    char procName[MAX_PROC_NAME_LEN];
    proc_name(pid, procName, sizeof(procName));

    // First, get the real iterator by calling the original function
    OSIterator *original_iterator = nullptr;
    if (original_IOService_getMatchingServices) {
        original_iterator = original_IOService_getMatchingServices(matching);
    }

    // If the process is not in our filter list, or if the original call failed,
    // return the original iterator immediately.
    if (!isProcFiltered(procName) || !original_iterator) {
        return original_iterator;
    }

    DBGLOG(MODULE_IOR, "Filtering getMatchingServices result for '%s' (PID: %d)", procName, pid);

    // Create a new array to hold the services we are NOT filtering out.
    OSArray *filtered_array = OSArray::withCapacity(16);
    if (!filtered_array) {
        DBGLOG(MODULE_ERROR, "Failed to allocate OSArray for filtering.");
        return original_iterator; // Return original on allocation failure
    }

    OSObject *object;
    while ((object = original_iterator->getNextObject())) {
        // We need to cast the object to IORegistryEntry to get its class name
        IORegistryEntry *entry = OSDynamicCast(IORegistryEntry, object);
        if (entry) {
            const char *className = entry->getMetaClass()->getClassName();
            
            // If the class name is NOT in our blocklist, add it to our results.
            if (!isClassFiltered(className)) {
                filtered_array->setObject(entry);
            } else {
                DBGLOG(MODULE_IOR, "Hiding class '%s' from process '%s'", className, procName);
            }
        }
    }
    
    // Release the original iterator, we are done with it.
    original_iterator->release();

    // Create a new iterator from our sanitized array.
	OSIterator *filtered_iterator = OSCollectionIterator::withCollection(filtered_array);
    
    // Release the array, the iterator now holds a reference to it.
    filtered_array->release();
    
    return filtered_iterator;
}

// Custom getMatchingService (Singular)
IOService *phtm_IOService_getMatchingService(OSDictionary *matching, IOService *from) {
    // First, get the real service by calling the original function
    IOService *original_service = nullptr;
    if (original_IOService_getMatchingService) {
        original_service = original_IOService_getMatchingService(matching, from);
    }

    // If the original call found nothing, just return.
    if (!original_service) {
        return nullptr;
    }
    
    proc_t p = current_proc();
    pid_t pid = proc_pid(p);
    char procName[MAX_PROC_NAME_LEN];
    proc_name(pid, procName, sizeof(procName));

    // If the process is one we are targeting and a service was found...
    if (isProcFiltered(procName)) {
        const char *className = original_service->getMetaClass()->getClassName();
          // Check if the returned service's class is on our blocklist.
        if (isClassFiltered(className)) {
            DBGLOG(MODULE_IOR, "Hiding single service of class '%s' from process '%s'", className, procName);
            // Don't release - just return nullptr to hide the service
            // The original function maintains ownership
            return nullptr;
        }
    }
    
    // Otherwise, return the service that was found.
    return original_service;
}

// Forward declaration for our main hook
OSObject *phtm_IORegistryEntry_getProperty_os_symbol(const IORegistryEntry *that, const OSSymbol *aKey);

// This is the new hook for the getProperty(const char*) variant.
OSObject *phtm_IORegistryEntry_getProperty_cstring(const IORegistryEntry *that, const char *aKey) {
    const OSSymbol *symbolKey = OSSymbol::withCString(aKey);
    OSObject *result = phtm_IORegistryEntry_getProperty_os_symbol(that, symbolKey);
    
    if (symbolKey) {
        const_cast<OSSymbol *>(symbolKey)->release();
    }
    return result;
}

// This is our main, updated hook for getProperty(const OSSymbol*).
OSObject *phtm_IORegistryEntry_getProperty_os_symbol(const IORegistryEntry *that, const OSSymbol *aKey) {
	
    // Always call the original function first to get the real property.
    OSObject* original_property = nullptr;
    if (original_IORegistryEntry_getProperty_os_symbol) {
        original_property = original_IORegistryEntry_getProperty_os_symbol(that, aKey);
    }
    
    proc_t p = current_proc();
    pid_t pid = proc_pid(p);
    char procName[MAX_PROC_NAME_LEN];
    proc_name(pid, procName, sizeof(procName));

    // Check if the process is one we want to target using our new helper function.
    if (isProcFiltered(procName))
    {
        const char *keyName = aKey->getCStringNoCopy();

        // If the key is "manufacturer", spoof it regardless of the class.
        if (keyName && strcmp(keyName, "manufacturer") == 0)
        {
            const char* entryClassName = that->getMetaClass()->getClassName();
            DBGLOG(MODULE_IOR, "'manufacturer' key on class '%s' called by '%s (PID: %d). Returning 'Apple Inc.'.",
                   entryClassName, procName, pid);
            
            // Return our new, spoofed OSString.
            return OSString::withCString("Apple Inc.");
        }
        else // If its not manufact, then lets go ahead and log what was being asked and what was returned
        {
            const char* entryClassName = that->getMetaClass()->getClassName();
            OSString* str_prop = OSDynamicCast(OSString, original_property);
            OSData* data_prop = OSDynamicCast(OSData, original_property);

            if (str_prop) {
                 DBGLOG(MODULE_IOR, "getProperty called by '%s' (PID: %d) on class '%s' for key '%s' returned OSString: '%s'",
                       procName, pid, entryClassName, keyName, str_prop->getCStringNoCopy());            } else if (data_prop) {
                 // Often, strings are stored in OSData. We can try to print it if it's null-terminated.
                 // Add bounds checking to prevent buffer overruns
                 uint32_t dataLen = data_prop->getLength();
                 if (dataLen > 0 && dataLen < 1024) { // Reasonable size limit
                     DBGLOG(MODULE_IOR, "getProperty called by '%s' (PID: %d) on class '%s' for key '%s' returned OSData (size: %d)",
                           procName, pid, entryClassName, keyName, dataLen);
                 } else {
                     DBGLOG(MODULE_IOR, "getProperty called by '%s' (PID: %d) on class '%s' for key '%s' returned OSData with size: %d (too large to display)",
                           procName, pid, entryClassName, keyName, dataLen);
                 }
            } else {
                 DBGLOG(MODULE_IOR, "getProperty called by '%s' (PID: %d) on class '%s' for key '%s' returned object of class '%s'",
                       procName, pid, entryClassName, keyName,
                       original_property ? original_property->getMetaClass()->getClassName() : "nullptr");
            }
        }
    }

    // For all other cases, just return the original property without logging.
    return original_property;
}

// Logs only for specified processes with more detail.
const char *phtm_IORegistryEntry_getName(const IORegistryEntry *that, const IORegistryPlane *plane) {
    const char *entryName = nullptr;
    if (original_IORegistryEntry_getName) {
        entryName = original_IORegistryEntry_getName(that, plane);
    }

    proc_t p = current_proc();
    pid_t pid = proc_pid(p);
    char procName[MAX_PROC_NAME_LEN];
    proc_name(pid, procName, sizeof(procName));

    // Only log if the process name matches one on our allow list, using the helper.
    if (isProcFiltered(procName))
    {
        // Get the class name for a more descriptive log message.
        const char* entryClassName = that->getMetaClass()->getClassName();
        DBGLOG(MODULE_IOR, "getName called by '%s' (PID: %d) on class '%s', returning name: '%s'",
               procName,
               pid,
               entryClassName ? entryClassName : "Unknown",
               entryName ? entryName : "Unknown");
    }

    return entryName;
}

// Custom getNextObject: Logs only for specified processes.
OSObject *phtm_IOIterator_getNextObject(OSCollectionIterator *that) {
    OSObject *obj = nullptr;
    if (original_IOIterator_getNextObject) {
        obj = original_IOIterator_getNextObject(that);
    }

    proc_t p = current_proc();
    pid_t pid = proc_pid(p);
    char procName[MAX_PROC_NAME_LEN];
    proc_name(pid, procName, sizeof(procName));
	
    // Only log if the process name matches one on our allow list, using the helper.
    if (isProcFiltered(procName))
    {
        DBGLOG(MODULE_IOR, "getNextObject called by '%s' (PID: %d) on an IOIterator, returning object of class '%s'",
               procName,
               pid,
               obj ? obj->getMetaClass()->getClassName() : "nullptr");
    }
    
    return obj;
}

// Generic rerouting function for simplicity
static bool reRoute(KernelPatcher &patcher, const char *mangledName, mach_vm_address_t replacement, void **original) {
    mach_vm_address_t address = patcher.solveSymbol(KernelPatcher::KernelID, mangledName);
    if (address) {
        DBGLOG(MODULE_RRIOR, "Resolved %s at 0x%llx", mangledName, address);
        *original = reinterpret_cast<void*>(patcher.routeFunction(address, replacement, true));

        if (patcher.getError() == KernelPatcher::Error::NoError && *original) {
            DBGLOG(MODULE_RRIOR, "Successfully routed %s", mangledName);
            return true;
        } else {
            DBGLOG(MODULE_ERROR, "Failed to route %s. Lilu error: %d", mangledName, patcher.getError());
            patcher.clearError();
        }
    } else {
        DBGLOG(MODULE_ERROR, "Failed to resolve symbol %s. Lilu error: %d", mangledName, patcher.getError());
        patcher.clearError();
    }
    return false;
}

// IORegistry Module Initialization
void IOR::init(KernelPatcher &Patcher) {
    DBGLOG(MODULE_IOR, "IOR::init(Patcher) called. IORegistry module is starting.");
    
    // getMatchingServices to hide entries from the start
    reRoute(Patcher, "__ZN9IOService19getMatchingServicesEP12OSDictionary",
            reinterpret_cast<mach_vm_address_t>(phtm_IOService_getMatchingServices),
            reinterpret_cast<void**>(&original_IOService_getMatchingServices));
	
    // getMatchingService (singular) to hide entries from direct lookups.
    // The mangled name is for IOService::getMatchingService(OSDictionary*, IOService*)
    reRoute(Patcher, "__ZN9IOService19getMatchingServiceEP12OSDictionaryP9IOService",
            reinterpret_cast<mach_vm_address_t>(phtm_IOService_getMatchingService),
            reinterpret_cast<void**>(&original_IOService_getMatchingService));
	
    // Reroute rest of the functions, Seq specific, it's 4am give me a break, ts free and oss
    reRoute(Patcher, "__ZNK15IORegistryEntry11getPropertyEPK8OSSymbol",
            reinterpret_cast<mach_vm_address_t>(phtm_IORegistryEntry_getProperty_os_symbol),
            reinterpret_cast<void**>(&original_IORegistryEntry_getProperty_os_symbol));
    
    reRoute(Patcher, "__ZNK15IORegistryEntry11getPropertyEPKc",
            reinterpret_cast<mach_vm_address_t>(phtm_IORegistryEntry_getProperty_cstring),
            reinterpret_cast<void**>(&original_IORegistryEntry_getProperty_cstring));
            
    reRoute(Patcher, "__ZNK15IORegistryEntry7getNameEPK15IORegistryPlane",
            reinterpret_cast<mach_vm_address_t>(phtm_IORegistryEntry_getName),
            reinterpret_cast<void**>(&original_IORegistryEntry_getName));

    reRoute(Patcher, "__ZN20OSCollectionIterator13getNextObjectEv",
            reinterpret_cast<mach_vm_address_t>(phtm_IOIterator_getNextObject),
            reinterpret_cast<void**>(&original_IOIterator_getNextObject));
	
    DBGLOG(MODULE_IOR, "IOR::init(Patcher) finished.");
}
