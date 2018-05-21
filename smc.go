package smc

/*
#cgo LDFLAGS: -framework IOKit

#include <stdlib.h>

#ifndef __SMC_H__
#import "smc.h"
#endif

*/
import "C"
import (
	"fmt"
	"sync"
	"unsafe"
)

// ReadTemperature returns the current temperature in celsius
func ReadTemperature() float64 {
	openMutex.Lock()
	if !open {
		C.SMCOpen()
	}
	cstr := C.CString("TC0P")
	temp := float64(C.SMCGetTemperature(cstr))
	C.free(unsafe.Pointer(cstr))
	if !open {
		C.SMCClose()
	}
	openMutex.Unlock()
	return temp
}

// ReadFanSpeeds returns the current fan speeds as rpm
func ReadFanSpeeds() []int {
	openMutex.Lock()
	if !open {
		C.SMCOpen()
	}
	speeds := make([]int, 0, 18)
	for i := 0; i < 18; i++ {
		cstr := C.CString(fmt.Sprintf("F%dAc", i))
		speed := int(C.SMCGetFanSpeed(cstr))
		C.free(unsafe.Pointer(cstr))
		if speed == 0 {
			break
		}
		speeds = append(speeds, speed)
	}
	if !open {
		C.SMCClose()
	}
	openMutex.Unlock()

	return speeds
}

// OpenSMC keeps the SMC connection open if desired, otherwise each call will do it
func OpenSMC() {
	openMutex.Lock()
	if !open {
		C.SMCOpen()
		open = true
	}
	openMutex.Unlock()
}

// CloseSMC closes the SMC connection
func CloseSMC() {
	openMutex.Lock()
	if open {
		C.SMCClose()
		open = false
	}
	openMutex.Unlock()
}

var open bool
var openMutex sync.Mutex
