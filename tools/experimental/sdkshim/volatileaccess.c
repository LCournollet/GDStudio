/* Local stand-in for volatileaccessu.lib from Windows SDK 10.0.26100, which is not
   installed here (SDK 10.0.22621 only). Geode's prebuilt libcurl references
   RtlSetVolatileMemory; this is the documented behaviour: a fill that the compiler
   may not elide (volatile byte stores). Used only to link the experimental loader. */
#include <stddef.h>
volatile void* RtlSetVolatileMemory(volatile void* Destination, int Fill, size_t Length) {
    volatile unsigned char* p = (volatile unsigned char*)Destination;
    while (Length--) *p++ = (unsigned char)Fill;
    return Destination;
}