// Package smc reads sensor values (temperatures, fan speeds) from the
// macOS AppleSMC kernel extension. It works on both Intel and Apple
// Silicon Macs; the difference is the set of keys and their data type:
//
//   - Intel: temperatures live under TC0P / TC0E / TC0F (sp78), fans
//     under F0Ac…F17Ac (fpe2).
//   - Apple Silicon: temperatures live under Tp01…Tp16 (flt — IEEE 754
//     single precision), fans under F0Ac…F17Ac (also flt).
//
// AppleSMC fan keys do not exist at all on fanless Macs (e.g. M1/M2
// MacBook Air); ReadFanSpeeds returns an empty slice on those.
package smc

/*
#cgo LDFLAGS: -framework IOKit

#include <stdlib.h>
#include "smc.h"
*/
import "C"
import (
	"encoding/binary"
	"fmt"
	"math"
	"sync"
	"unsafe"
)

// readRaw reads an SMC key and returns its 4-character type code and
// raw bytes. ok=false means the key doesn't exist or the read failed.
func readRaw(key string) (typeCode string, data []byte, ok bool) {
	if len(key) != 4 {
		return "", nil, false
	}
	ckey := C.CString(key)
	defer C.free(unsafe.Pointer(ckey))

	var ctype [4]C.char
	var csize C.uint32_t
	var cbuf [32]C.uint8_t

	if C.SMCRead(ckey, &ctype[0], &csize, &cbuf[0]) != 0 {
		return "", nil, false
	}
	typeCode = C.GoStringN(&ctype[0], 4)
	n := int(csize)
	out := make([]byte, n)
	for i := 0; i < n; i++ {
		out[i] = byte(cbuf[i])
	}
	return typeCode, out, true
}

// decode converts an SMC value to a float64 based on its type code.
// Supported types:
//
//	sp78  signed 8.8 fixed-point (Intel temperature) — °C
//	fpe2  unsigned 14.2 fixed-point (Intel fan RPM)
//	flt   IEEE 754 single-precision (Apple Silicon temperature & fan)
//	ui8   unsigned 8-bit integer
//	ui16  unsigned 16-bit integer
//	ui32  unsigned 32-bit integer
//
// Returns ok=false for unrecognized type codes or short buffers.
func decode(typeCode string, data []byte) (value float64, ok bool) {
	switch typeCode {
	case "sp78":
		if len(data) < 2 {
			return 0, false
		}
		return float64(int16(binary.BigEndian.Uint16(data))) / 256.0, true
	case "fpe2":
		if len(data) < 2 {
			return 0, false
		}
		return float64(binary.BigEndian.Uint16(data)) / 4.0, true
	case "flt ":
		if len(data) < 4 {
			return 0, false
		}
		return float64(math.Float32frombits(binary.LittleEndian.Uint32(data))), true
	case "ui8 ":
		if len(data) < 1 {
			return 0, false
		}
		return float64(data[0]), true
	case "ui16":
		if len(data) < 2 {
			return 0, false
		}
		return float64(binary.BigEndian.Uint16(data)), true
	case "ui32":
		if len(data) < 4 {
			return 0, false
		}
		return float64(binary.BigEndian.Uint32(data)), true
	}
	return 0, false
}

// readFloat is the common readRaw + decode path. ok=false on any failure.
func readFloat(key string) (value float64, ok bool) {
	t, b, found := readRaw(key)
	if !found {
		return 0, false
	}
	return decode(t, b)
}

// Intel CPU proximity temperature key, in sp78 format.
const intelCPUTempKey = "TC0P"

// Apple Silicon per-CPU-cluster temperature keys, in flt format.
// Each is a separate sensor; not all exist on every chip variant
// (e.g. M1 reports up to Tp09; M2 Max populates more). We scan all
// and return whatever the kernel acknowledges.
func appleSiliconCPUTempKeys() []string {
	keys := make([]string, 0, 16)
	for i := 1; i <= 16; i++ {
		keys = append(keys, fmt.Sprintf("Tp%02d", i))
	}
	return keys
}

// ReadTemperature returns a representative CPU temperature in degrees
// Celsius, or 0.0 if no temperature sensor could be read.
//
//   - On Intel, this is the TC0P (CPU proximity) sensor.
//   - On Apple Silicon, this is the maximum across all populated
//     per-cluster CPU sensors (Tp01…Tp16) — i.e. the hottest core
//     cluster, which is generally the most useful single value for
//     thermal monitoring.
func ReadTemperature() float64 {
	openMutex.Lock()
	autoOpen := !open
	if autoOpen {
		C.SMCOpen()
	}
	defer func() {
		if autoOpen {
			C.SMCClose()
		}
		openMutex.Unlock()
	}()

	if v, ok := readFloat(intelCPUTempKey); ok && v != 0 {
		return v
	}
	hottest := 0.0
	for _, k := range appleSiliconCPUTempKeys() {
		if v, ok := readFloat(k); ok && v > hottest {
			hottest = v
		}
	}
	return hottest
}

// ReadTemperatures returns every CPU-cluster temperature sensor the
// kernel acknowledges, keyed by SMC key (e.g. "Tp01"). On Intel this
// will typically be a single entry under "TC0P"; on Apple Silicon it
// will be several Tp* entries. Empty if no sensors respond.
func ReadTemperatures() map[string]float64 {
	openMutex.Lock()
	autoOpen := !open
	if autoOpen {
		C.SMCOpen()
	}
	defer func() {
		if autoOpen {
			C.SMCClose()
		}
		openMutex.Unlock()
	}()

	out := make(map[string]float64)
	if v, ok := readFloat(intelCPUTempKey); ok && v != 0 {
		out[intelCPUTempKey] = v
	}
	for _, k := range appleSiliconCPUTempKeys() {
		if v, ok := readFloat(k); ok && v != 0 {
			out[k] = v
		}
	}
	return out
}

// ReadFanSpeeds returns the current speed of each active fan in RPM,
// in fan-index order. Returns an empty slice on fanless Macs (M1/M2
// MacBook Air, etc.) where the SMC has no F*Ac keys.
//
// Reads keys F0Ac through F17Ac and stops at the first one that does
// not respond. AppleSMC reports these as sp78/fpe2 on Intel and as
// IEEE-754 float on Apple Silicon; both formats are handled.
func ReadFanSpeeds() []int {
	openMutex.Lock()
	autoOpen := !open
	if autoOpen {
		C.SMCOpen()
	}
	defer func() {
		if autoOpen {
			C.SMCClose()
		}
		openMutex.Unlock()
	}()

	speeds := make([]int, 0, 4)
	for i := 0; i < 18; i++ {
		key := fmt.Sprintf("F%dAc", i)
		v, ok := readFloat(key)
		if !ok {
			break
		}
		speeds = append(speeds, int(v))
	}
	return speeds
}

// OpenSMC keeps the SMC connection open for the lifetime of the
// process. Otherwise each Read* call opens and closes it.
func OpenSMC() {
	openMutex.Lock()
	defer openMutex.Unlock()
	if !open {
		C.SMCOpen()
		open = true
	}
}

// CloseSMC closes the persistent SMC connection opened by OpenSMC.
func CloseSMC() {
	openMutex.Lock()
	defer openMutex.Unlock()
	if open {
		C.SMCClose()
		open = false
	}
}

var (
	open      bool
	openMutex sync.Mutex
)
