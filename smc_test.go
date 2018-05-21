package smc

import "testing"

func TestCPU(t *testing.T) {
	temp := ReadTemperature()
	if temp == 0 {
		t.Errorf("Temp %f", temp)
	}
}

func TestFans(t *testing.T) {
	speeds := ReadFanSpeeds()
	if len(speeds) == 0 {
		t.Errorf("Speeds %+v", speeds)
	}
}

func TestOpen(t *testing.T) {
	OpenSMC()
	TestCPU(t)
	TestFans(t)
	CloseSMC()
}
