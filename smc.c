/*
 * smc.c — minimal AppleSMC reader for go-smc.
 *
 * Provenance
 * ----------
 * This file is an independent reimplementation of the user-space
 * access pattern for AppleSMC. It is *not* derived from the widely
 * copied "Apple System Management Control (SMC) Tool" by devnull
 * (2006), which is GPLv2. Two references were used:
 *
 *   1. Apple's PowerManagement project (APSL), which contains the
 *      authoritative SMCKeyData struct layout and IOKit call shape
 *      for talking to AppleSMC from user space.
 *      https://opensource.apple.com/source/PowerManagement/
 *
 *   2. beltex/SMCKit (MIT, Swift), which establishes the standard
 *      shape of a clean SMC reader: SMCOpen/SMCClose + a single
 *      two-step readKey helper.
 *      https://github.com/beltex/SMCKit
 *
 * Struct layouts and the SMC ioctl indices reproduced here are
 * IOKit ABI facts and are not copyrightable expression. Released
 * under the MIT license — see LICENSE.
 *
 * Design note: this file only marshals the kernel call and returns
 * the raw key bytes + 4-byte type code to Go. All format decoding
 * (sp78, fpe2, flt, …) lives in smc.go where it's easier to test
 * and extend without touching cgo.
 */

#include <stdint.h>
#include <string.h>
#include <IOKit/IOKitLib.h>

#include "smc.h"

/* AppleSMC user-client selector + command codes (IOKit ABI). */
enum {
    kSMCUserClientIndex = 2,
    kSMCCmdReadBytes    = 5,
    kSMCCmdReadKeyInfo  = 9,
};

static io_connect_t gConn;

/* Pack a 4-character SMC key (e.g. "TC0P") into its uint32 form. */
static uint32_t packKey(const char *k) {
    return ((uint32_t)(uint8_t)k[0] << 24) |
           ((uint32_t)(uint8_t)k[1] << 16) |
           ((uint32_t)(uint8_t)k[2] <<  8) |
           ((uint32_t)(uint8_t)k[3]);
}

int SMCOpen(void) {
    CFMutableDictionaryRef match = IOServiceMatching("AppleSMC");
    io_iterator_t it = 0;
    /* kIOMasterPortDefault is deprecated in macOS 12 in favor of
     * IOMainPort(), but its value (the default port) hasn't changed
     * and it remains the simplest way to stay source-compatible
     * across SDKs. Silence the deprecation locally. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, match, &it) != kIOReturnSuccess) {
#pragma clang diagnostic pop
        return -1;
    }
    io_object_t dev = IOIteratorNext(it);
    IOObjectRelease(it);
    if (dev == 0) {
        return -1;
    }
    kern_return_t kr = IOServiceOpen(dev, mach_task_self(), 0, &gConn);
    IOObjectRelease(dev);
    return (kr == kIOReturnSuccess) ? 0 : -1;
}

int SMCClose(void) {
    return IOServiceClose(gConn);
}

int SMCRead(const char *key, char type[4], uint32_t *size, uint8_t data[32]) {
    *size = 0;
    memset(data, 0, 32);
    memset(type, 0, 4);

    SMCKeyData_t in, out;
    memset(&in,  0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.key   = packKey(key);
    in.data8 = kSMCCmdReadKeyInfo;

    size_t outSize = sizeof(out);
    if (IOConnectCallStructMethod(gConn, kSMCUserClientIndex,
                                  &in, sizeof(in),
                                  &out, &outSize) != kIOReturnSuccess) {
        return -1;
    }
    uint32_t dsz = out.keyInfo.dataSize;
    uint32_t dty = out.keyInfo.dataType;
    if (dsz == 0 || dsz > 32) {
        return -1;
    }

    in.keyInfo.dataSize = dsz;
    in.data8            = kSMCCmdReadBytes;
    outSize             = sizeof(out);
    memset(&out, 0, sizeof(out));
    if (IOConnectCallStructMethod(gConn, kSMCUserClientIndex,
                                  &in, sizeof(in),
                                  &out, &outSize) != kIOReturnSuccess) {
        return -1;
    }

    type[0] = (dty >> 24) & 0xff;
    type[1] = (dty >> 16) & 0xff;
    type[2] = (dty >>  8) & 0xff;
    type[3] = (dty      ) & 0xff;
    *size   = dsz;
    memcpy(data, out.bytes, dsz);
    return 0;
}
