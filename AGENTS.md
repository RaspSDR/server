# AGENTS.md

## Scope

This file applies to the entire repository.

Web-888 Server is a C/C++ WebSDR server derived from KiwiSDR. The production
target is the Xilinx Zynq-7010 SoC running 32-bit ARMv7 Alpine Linux with musl.
Treat ARM/Alpine behavior as authoritative.

## Target constraints

- Build production artifacts for ARMv7-A hard-float, Cortex-A9, and NEON.
- Use GCC. The CMake configuration expects GNU binutils for post-build
  `objcopy` and `strip` processing.
- Build and link in an Alpine ARM environment. Do not assume glibc behavior or
  validate a release only on x86.
- The expected executable is `websdr.bin`, an ARM EABI5 binary using
  `/lib/ld-musl-armhf.so.1`.
- Keep the Zynq hardware path enabled. `main.cpp` selects
  `PLATFORM_ZYNQ7010`, and `zynq/` accesses the FPGA and `/dev/zynqsdr`.
- Do not add x86-only SIMD, unaligned-access, libc, or compiler assumptions.
  Preserve the existing ARM NEON and hard-float flags unless intentionally
  changing the hardware baseline.
- The runtime expects root-level access and target paths including
  `/root/config`, `/media/mmcblk0p1/config/samples`, `/tmp/kiwi.data`,
  `/dev/zynqsdr`, and `/dev/xdevcfg`.
- Alpine's native `fdk-aac` 2.0.2 is known to crash in the DRM decoder. The
  build copies the repository's ARM32 library from
  `extensions/DRM/FDK-AAC/lib/arm32/`; do not replace it casually.

## Repository layout

- `main.cpp`: process startup, argument handling, platform selection, and
  service initialization.
- `native/`: fake Zynq hardware, EEPROM, and system-control implementations
  used only by the native browser harness.
- `zynq/`: Zynq-7010 hardware integration, FPGA loading, peripherals, and
  Linux driver access.
- `rx/`: receiver, audio, waterfall, filtering, and DSP data paths.
- `net/`: networking, services, updates, MQTT, and connection buffers.
- `support/`: threading/coroutine compatibility, shared memory, diagnostics,
  timing, strings, and common runtime utilities.
- `init/`: configuration, clocks, DX data, and startup helpers.
- `gps/`: hardware GPS and PPS-related support.
- `extensions/`: native decoder and receiver extensions. CMake discovers most
  extension C/C++ sources recursively.
- `web/`: browser UI, JavaScript extensions, CSS, HTML, and assets embedded
  into the server binary.
- `unix_env/kiwi.config/`: configuration templates and sample data copied into
  the build output.
- `pkgs/`: bundled third-party code; avoid broad cleanup or style-only edits.
- `externals/dumphfdl/`: HFDL submodule and external CMake build.
- `tests/`: small standalone tests; there is not currently a comprehensive
  CTest suite. `tests/native_browser_smoke.js` drives the native server through
  a real headless browser.
- `tools/`: scripts for creating and entering the QEMU-backed Alpine ARM
  chroot. `tools/test-native-browser.sh` builds and runs the native browser
  harness.
- `build/`: generated output. Never hand-edit or commit generated files from
  this directory.

## Build environment

Initialize submodules before the first build:

```sh
git submodule update --init --recursive
```

The supported development setup is an Alpine ARMv7 root filesystem executed
through `qemu-arm-static` on a Linux host. `tools/setup.sh` creates the Alpine
root and installs the required packages; `tools/run.sh` bind-mounts related
trees and enters the chroot. These scripts require root privileges and should
be reviewed before running because they create mounts and modify a chroot.

Core Alpine build dependencies include GCC/G++, CMake, make, pkgconf,
musl-dev, linux-headers, FFTW, zlib, GPSD, libunwind, SQLite, libconfig,
libcurl, OpenSSL, FDK-AAC development files, Perl, and `minify`. HFDL also
uses autotools and builds external dependencies.

## Configure and build

Run the production build inside the Alpine ARMv7 environment:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For a faster build when HFDL is unrelated to the change:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_HDFL=OFF
cmake --build build --parallel
```

Important build behavior:

- `ZYNQ` and `ENABLE_HDFL` default to `ON`.
- CMake selects ARMv7-A/Cortex-A9/NEON/hard-float flags only when the build
  environment reports an ARM processor.
- A non-ARM build enters the `EMULATOR` branch and disables HFDL. It may be
  useful as a compile smoke check, but it is not release validation.
- Release builds minify web assets. Missing `minify` can break asset
  generation.
- CMake generates `build/extint.cpp`, `build/version.h`, `build/EiBi.h`, the
  `build/edata_*.cpp` files, copied configuration, and embedded web assets.
- Configuration downloads the current EiBi schedule and HFDL may download and
  build external projects, so a clean build can require network access.
- The post-build step strips `websdr.bin` and writes symbols to `websdr.dbg`.

## Native emulator and browser harness

A regular non-ARM build automatically enters the `EMULATOR` CMake branch. This
removes ARM compiler flags and disables HFDL, but by itself is only a compile
smoke check. It still uses the production Zynq runtime interfaces.

Set `NATIVE_HARNESS=ON` to replace the Zynq interfaces with implementations
from `native/`. The harness:

- exposes four receiver and four waterfall channels;
- generates deterministic complex tones for receiver data and multiple
  carriers for waterfall data;
- stores runtime configuration under `build-native/config`;
- avoids root-only core-dump setup and SD-card configuration copies;
- disables GPS, public registration, updates, LED control, and other external
  target services;
- ignores reboot, halt, and power-off requests;
- excludes DRM because its decoder processes depend on the target FDK-AAC
  runtime.

Install the browser dependency and Playwright Chromium once:

```sh
npm install
npx playwright install chromium
```

Run the complete native build and browser smoke test:

```sh
npm run test:native-browser
```

The script uses all CPUs reported by `nproc`, starts `websdr.bin`, checks the
`/status` endpoint, and launches headless Chromium. The browser test requires
the sound and waterfall WebSockets to reach the open state and verifies that
received fake data advances the waterfall canvas.

The default test port is 8073. Override it when necessary:

```sh
WEBSDR_HARNESS_PORT=18073 npm run test:native-browser
```

Use `BUILD_JOBS` to override the automatic build parallelism and
`WEBSDR_HARNESS_BUILD_DIR` to select a different generated build directory.
Do not commit `build-native/` or `node_modules/`.

## Validation

Use the smallest relevant checks, but complete target validation on Alpine
ARMv7:

```sh
cmake --build build --parallel
file build/websdr.bin
```

The `file` output should identify a 32-bit ARM EABI5 executable with the musl
ARM hard-float interpreter. For the existing standalone JavaScript test:

```sh
node tests/test_ctc.js
```

Hardware-sensitive changes require testing on a Zynq-7010 device with the
matching kernel driver and FPGA bitstream. QEMU or x86 builds cannot validate
FPGA loading, `/dev/zynqsdr` I/O, PPS timing, ADC clock tuning, real-time DSP,
or receiver concurrency. A successful native browser-harness run is not
production or device validation.

### WEB-888 hardware validation

The development receiver is reachable as `root@web-888.local`. Check that no
users are connected before interrupting the production service:

```sh
curl -fsS http://web-888.local:8073/status
```

Build the ARM release using the host Alpine environment, then verify the
artifact before deployment:

```sh
sudo ~/alpine/build
file build/websdr.bin
sha256sum build/websdr.bin
```

The build script writes the target binary to `build/websdr.bin` in this
checkout. The `file` output must identify a 32-bit ARM EABI5 executable using
`/lib/ld-musl-armhf.so.1`.

Stop the supervised service, preserve the installed binary, and deploy through
a temporary path so `/root/websdr.bin` is never partially written:

```sh
ssh root@web-888.local \
  '/etc/init.d/sdrd stop && cp -p /root/websdr.bin /root/websdr.bin.pre-test'
scp build/websdr.bin root@web-888.local:/root/websdr.bin.test
ssh root@web-888.local \
  'chmod 755 /root/websdr.bin.test && mv /root/websdr.bin.test /root/websdr.bin'
```

Run the private binary in the foreground from a dedicated terminal so startup
and hardware errors remain visible:

```sh
ssh -t root@web-888.local 'cd /root && ./websdr.bin'
```

Confirm FPGA and peripheral initialization, the expected receiver/waterfall
channel counts, and the HTTP listener. From another terminal, verify `/status`
and exercise the affected behavior through a real browser. For receiver or UI
changes, confirm live sound and waterfall WebSockets, advancing waterfall
data, and the specific modified controls or rendering paths.

After testing, stop the foreground process and return the receiver to
supervised operation. Restore the saved binary first when the test build
should not remain installed:

```sh
ssh root@web-888.local \
  'cp -p /root/websdr.bin.pre-test /root/websdr.bin && /etc/init.d/sdrd start'
```

If retaining the tested binary, omit the copy and start `sdrd`. In either case,
recheck the remote checksum, service status, process list, and `/status`
version. Boot or service-management scripts may restore the SD-card-managed
production image, so do not assume the manually tested binary remains active.

If the receiver reboots and its SSH host key changes, stop and independently
verify the new fingerprint. Never disable strict host-key checking or remove
the existing `known_hosts` entry merely to continue a test.

## Change guidelines

- Follow the existing C/C++ style in the file being changed. The codebase
  generally uses four-space indentation, C-style interfaces, project typedefs
  from `types.h`, and local error/assertion helpers.
- Preserve C++11 compatibility.
- Treat receiver, networking, extension, and shared-memory paths as
  concurrent. Maintain existing locking and avoid introducing blocking work
  into timing-sensitive DSP paths.
- Keep hot-path allocations, copies, logging, and floating-point conversions
  under control; the Cortex-A9 has limited CPU and memory headroom.
- Use fixed-width/project integer types where wire formats, FPGA registers,
  shared memory, or persisted data require an exact layout.
- Check return values for system calls and hardware I/O using the repository's
  established error-reporting patterns. Do not silently continue after device,
  configuration, or asset-generation failures.
- When adding an extension, update the `EXTENSIONS` list in `CMakeLists.txt`
  and add matching browser assets under `web/extensions/` when required.
- When changing web assets, rebuild so the minified copies and embedded
  `edata_*.cpp` inputs are regenerated; never edit generated copies in
  `build/htdoc/`.
- Do not modify vendored or submodule code unless the task specifically
  requires it. Keep such changes isolated from first-party changes.
- Do not commit binaries, debug symbols, generated configuration, downloaded
  schedules, or other contents of `build/`.

## Runtime and deployment

The build output is deployed to the target filesystem/TF card as
`websdr.bin`, together with the expected configuration, sample data, FPGA
bitstreams, kernel driver, and compatible shared libraries. Do not claim a
change is device-ready based only on successful compilation: verify startup,
receiver operation, web access, and the affected extension or hardware path on
the Zynq-7010 Alpine target.
