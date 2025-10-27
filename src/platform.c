// Platform abstraction layer implementation

#include "platform.h"

// Convert interrupt reason code to human-readable string
const char* platform_int_reason_str(uint32_t reason)
{
    switch (reason) {
        case PLATFORM_INT_TIMEOUT:   return "Timeout";
        case PLATFORM_INT_TIMER:     return "Timer";
        case PLATFORM_INT_DEVICE:    return "Device";
        case PLATFORM_INT_IPI:       return "IPI";
        case PLATFORM_INT_EXCEPTION: return "Exception";
        default:                     return "Unknown";
    }
}
