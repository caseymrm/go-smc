/*
 * smc.h — minimal AppleSMC user-space interface for go-smc.
 *
 * Independent reimplementation. See smc.c for provenance and
 * licensing notes. MIT licensed — see LICENSE.
 */

#ifndef GO_SMC_H
#define GO_SMC_H

#include <stdint.h>

/*
 * SMCKeyData_t is the request/response struct AppleSMC expects from
 * user space. The layout is part of the IOKit ABI exposed by the
 * AppleSMC kernel extension and appears in Apple's APSL-licensed
 * PowerManagement source; field shapes are reproduced here so the
 * kernel call marshals correctly. Field naming follows beltex/SMCKit
 * (MIT) where it diverges from Apple's headers.
 */

typedef struct {
    uint8_t  major;
    uint8_t  minor;
    uint8_t  build;
    uint8_t  reserved;
    uint16_t release;
} SMCVersion;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t cpuPLimit;
    uint32_t gpuPLimit;
    uint32_t memPLimit;
} SMCPLimitData;

typedef struct {
    uint32_t dataSize;
    uint32_t dataType;
    uint8_t  dataAttributes;
} SMCKeyInfo;

typedef struct {
    uint32_t      key;
    SMCVersion    vers;
    SMCPLimitData pLimitData;
    SMCKeyInfo    keyInfo;
    uint8_t       result;
    uint8_t       status;
    uint8_t       data8;
    uint32_t      data32;
    uint8_t       bytes[32];
} SMCKeyData_t;

int SMCOpen(void);
int SMCClose(void);

/*
 * SMCRead performs the two-call AppleSMC read protocol for `key`
 * (a 4-character string, e.g. "TC0P" / "F0Ac" / "Tp01"). On success
 * it returns 0 and fills:
 *   type[4]    — the SMC data type as ASCII (e.g. "sp78", "fpe2", "flt ")
 *   *size      — the number of valid bytes in data[]
 *   data[32]   — the raw key bytes; caller decodes based on `type`.
 * On failure returns non-zero; *size and data[] are zeroed.
 */
int SMCRead(const char *key, char type[4], uint32_t *size, uint8_t data[32]);

#endif /* GO_SMC_H */
