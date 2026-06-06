# go-smc

Golang library to read temperatures and fan speeds from the macOS System Management Controller (SMC). Works on both Intel and Apple Silicon Macs.

```go
go get github.com/caseymrm/go-smc
```

```go
smc.OpenSMC()
defer smc.CloseSMC()

fmt.Println("CPU temperature:", smc.ReadTemperature(), "°C")
fmt.Println("Fan speeds:",       smc.ReadFanSpeeds(),  "RPM")

for key, tempC := range smc.ReadTemperatures() {
    fmt.Printf("  %s = %.2f °C\n", key, tempC)
}
```

## Apple Silicon

Both fan and temperature reads go through AppleSMC on all architectures — only the keys and data type differ:

| Reading        | Intel                | Apple Silicon            |
|----------------|----------------------|--------------------------|
| CPU temperature | `TC0P` (sp78)        | `Tp01`…`Tp16` (flt)      |
| Fan speed       | `F0Ac`…`F17Ac` (fpe2) | `F0Ac`…`F17Ac` (flt)    |

`ReadTemperature()` returns the hottest sensor across whatever keys the SoC populates (M1 reports fewer than M2/M3/M4). `ReadTemperatures()` returns the full map of populated sensors if you need per-cluster detail.

Fanless Macs (M1/M2 MacBook Air, Mac mini, etc.) have no `F*Ac` keys at all; `ReadFanSpeeds()` returns an empty slice there.

## License

MIT — see [LICENSE](LICENSE).

The C in `smc.c` / `smc.h` is an independent reimplementation written against two references, both cited in the file headers:

- Apple's APSL-licensed [PowerManagement](https://opensource.apple.com/source/PowerManagement/) source, for the AppleSMC IOKit ABI (struct layout, ioctl indices).
- [beltex/SMCKit](https://github.com/beltex/SMCKit) (MIT, Swift), for the shape of a clean reader.

It is **not** derived from the GPL "Apple SMC Tool" by devnull (2006) that's copied across many SMC projects. Earlier versions of this repo bundled that GPL file under an MIT `LICENSE`, which was an error; [#1](https://github.com/caseymrm/go-smc/issues/1) tracked and resolved that conflict.
