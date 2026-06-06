# go-smc

Golang library to read temperature and fan speeds from the macOS System Management Controller (SMC).

```go
go get github.com/caseymrm/go-smc
```

```go
smc.OpenSMC()
defer smc.CloseSMC()
fmt.Println(smc.ReadTemperature(), smc.ReadFanSpeeds())
```

## Status

Intel only. Apple Silicon Macs have no AppleSMC fan or temperature keys to read,
so this library has no useful behavior on `darwin/arm64`. New work should target
[powermetrics](https://www.unix.com/man-page/osx/1/powermetrics/) or
[IOHIDEventSystem](https://developer.apple.com/documentation/iokit) instead.

## License

MIT — see [LICENSE](LICENSE).

The C implementation in `smc.c` / `smc.h` is an independent reimplementation
written against two references:

- Apple's APSL-licensed [PowerManagement](https://opensource.apple.com/source/PowerManagement/)
  source, for the AppleSMC IOKit ABI (struct layout, ioctl indices).
- [beltex/SMCKit](https://github.com/beltex/SMCKit) (MIT, Swift), for the
  shape of a clean reader (single two-step `readKey`, `sp78`/`fpe2` decoders).

It is **not** derived from the GPL "Apple SMC Tool" by devnull (2006) that is
copied across many SMC projects. Earlier versions of this repo bundled that
GPL file under an MIT `LICENSE`, which was an error;
[#1](https://github.com/caseymrm/go-smc/issues/1) tracked that conflict and
this commit resolves it.
