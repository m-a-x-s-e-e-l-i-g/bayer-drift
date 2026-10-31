# Building Bayer Drift

You do not need to compile anything to use the [release download](https://github.com/m-a-x-s-e-e-l-i-g/bayer-drift/releases/latest).

## Build environment

The project uses Python 3.10+, Git, GNU Make and an ARM bare-metal toolchain with newlib. The released module was built with `arm-none-eabi-gcc 13.2.1` through Ubuntu WSL. A pinned-source Docker builder is also provided.

On Windows, install Ubuntu WSL and the toolchain inside Ubuntu. On Ubuntu/Debian, the relevant packages are `build-essential gcc-arm-none-eabi libnewlib-arm-none-eabi`. Use a Python installation with access to Git. The build helper obtains Magic Lantern source at revision `ed3e7c0dfacfdeaf3349dee73edf9e3621217462` and refuses a different or modified checkout.

Download the [official 2025-06-20 EOSM.202 archive](https://builds.magiclantern.fm/builds/dev/magiclantern.2025-06-20.EOSM.202.zip) to `.cache/magiclantern.2025-06-20.EOSM.202.zip`. The import audit checks its SHA-256 against `152f2ab46b83f45b978bcb704bda976849aa9d06b08fb70448f69be3ebfc44fb`. See [export-table provenance](../targets/EOSM.202/PROVENANCE.md).

From the repository root:

```text
# Windows host, Ubuntu WSL toolchain
python tools/build_module.py --module bayer --runtime wsl

# Linux host, installed toolchain
python3 tools/build_module.py --module bayer --runtime native

# Docker alternative
python tools/build_module.py --module bayer --runtime docker
```

The output is `build/modules/bayer/bayer.mo`, with its build manifest, ELF report and import audit. Only install the `.mo` on the camera. The build does not produce a replacement Magic Lantern core or Canon firmware.

## Test

```text
python scripts/test_bayer.py --full
```

This runs host sanitizers, full EOS M RAW geometry, capture, settings and low-memory fixtures. The runner uses Ubuntu WSL on Windows and native tools on Linux.

For the complete release suite:

```text
python scripts/test_bayer.py --arm --full
```

This also requires an EOS-capable QEMU executable at `build/qemu-eos/qemu-system-arm` with `versatilepb`, semihosting and both `arm946-eos` and `arm946-eos5` CPU models. Standard QEMU ARM builds do not provide those EOS CPU models. The release suite used the [Magic Lantern QEMU fork](https://github.com/reticulatedpines/qemu-eos), branch `qemu-eos-v4.2.1`, revision `4b667a1d3c08ab7a55835d15ddbd884fa754946d`, configured for `arm-softmmu` and plugins. Follow its upstream build instructions and place the executable at the path above. QEMU and Canon ROMs are not included in this repository or the release ZIP.

Tests write timestamped evidence under `build/bayer-tests`. The release ZIP contains the logs used to validate its exact module hash.

## Package

After the full ARM suite passes, supply its `results.json`:

```text
python tools/package_bayer.py --evidence build/bayer-tests/<run>/results.json
```

This creates a development candidate ZIP. Public releases additionally require `--release`, the owner's hardware report and the binary-runtime comparison recorded in `release-manifests/0.1.0`. Fixture evidence remains labelled as automated testing, separate from hardware reports. All package inputs are hash checked; existing outputs are never overwritten.

The fixed default build timestamp is part of the reproducible module metadata. The release's calendar date is recorded separately in the changelog and hardware report. Exact compiler/toolchain versions still matter when reproducing binary hashes.
