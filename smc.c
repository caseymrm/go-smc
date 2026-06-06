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
 *      two-step readKey helper that decodes sp78 (temperature) and
 *      fpe2 (fan RPM) fixed-point formats.
 *      https://github.com/beltex/SMCKit
 *
 * Struct layouts and the SMC ioctl indices reproduced here are
 * IOKit ABI facts and are not copyrightable expression. Released
 * under the MIT license — see LICENSE.
 */

#include <stdint.h>
#include <string.h>
#include <IOKit/IOKitLib.h>

#include "smc.h"

/* AppleSMC user-client selector + command codes (IOKit ABI). */
enum {
    kSMCUserClientIndex   = 2,
    kSMCCmdReadBytes      = 5,
    kSMCCmdReadKeyInfo    = 9,
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

/*
 * readKey performs the two-call SMC read protocol: ask for the key's
 * metadata (size + type), then ask for that many bytes. On success,
 * `out` holds the response from the second call (bytes[] is valid).
 */
static int readKey(const char *key, SMCKeyData_t *out) {
    SMCKeyData_t in;
    memset(&in, 0, sizeof(in));
    memset(out, 0, sizeof(*out));
    in.key   = packKey(key);
    in.data8 = kSMCCmdReadKeyInfo;

    size_t outSize = sizeof(*out);
    if (IOConnectCallStructMethod(gConn, kSMCUserClientIndex,
                                  &in, sizeof(in),
                                  out, &outSize) != kIOReturnSuccess) {
        return -1;
    }

    in.keyInfo.dataSize = out->keyInfo.dataSize;
    in.data8            = kSMCCmdReadBytes;
    outSize             = sizeof(*out);
    if (IOConnectCallStructMethod(gConn, kSMCUserClientIndex,
                                  &in, sizeof(in),
                                  out, &outSize) != kIOReturnSuccess) {
        return -1;
    }
    return 0;
}

/*
 * sp78 is Apple's signed 8.8 fixed-point format used for temperature
 * keys (e.g. TC0P, "CPU 0 proximity"): high byte = signed integer
 * part, low byte = fractional part / 256.
 */
double SMCGetTemperature(char *key) {
    SMCKeyData_t r;
    if (readKey(key, &r) != 0) {
        return 0.0;
    }
    int16_t raw = (int16_t)(((uint16_t)r.bytes[0] << 8) | (uint16_t)r.bytes[1]);
    return raw / 256.0;
}

/*
 * fpe2 is Apple's unsigned 14.2 fixed-point format used for fan
 * keys (e.g. F0Ac, "fan 0 actual"): 14-bit integer RPM in the high
 * bits, 2 fractional bits in the low — divide the raw u16 by 4.
 */
double SMCGetFanSpeed(char *key) {
    SMCKeyData_t r;
    if (readKey(key, &r) != 0) {
        return 0.0;
    }
    uint16_t raw = ((uint16_t)r.bytes[0] << 8) | (uint16_t)r.bytes[1];
    return raw / 4.0;
}
