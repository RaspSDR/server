# Web-888 Web Server Code

This code is forked from KiwiSDR project.

## The notable changes:

1. Use Linux Kernel Driver to interactive with hardware. No strict timing requirement on code.
1. Use Linux Pthread scheduler to replace userland task scheduler. Add lock protection in many code paths, which was assumed running in single core.
1. Use native thread instead of process for most blocking opertaions.
1. Use hardware GPS instead of Software Defined GPS receiver
1. Use PPS signal to tune ADC clock.
1. Disable ToDA extention for now
1. Use CMake as build system instead of Makefile, gcc as compiler instead of clang.

## Setup a build enviorment by qemu

The instruction is only tested on Debian. It may work on Ubuntu as well but not testsed.

1. Install necessary packages
```
# sudo apt install qemu-user-static binfmt-support wget
```

2. Create a virtual ARM enviroment (!!not a VM!!)
```
# mkdir ~/alpine
# cd ~/alpine
# wget http://dl-cdn.alpinelinux.org/alpine/v3.20/main/armv7/apk-tools-static-2.14.4-r1.apk
# mkdir alpine-apk
# tar -zxf apk-tools-static-2.14.4-r1.apk --directory=alpine-apk --warning=no-unknown-keyword
# mkdir -p alpine-root/usr/bin
# cp /usr/bin/qemu-arm-static alpine-root/usr/bin/
# mkdir -p alpine-root/etc
# cp /etc/resolv.conf alpine-root/etc/
# cp -r alpine-apk/sbin alpine-root/
# sudo chroot alpine-root /sbin/apk.static --repository http://dl-cdn.alpinelinux.org/alpine/v3.20/main --update-cache --allow-untrusted --initdb add alpine-base
```

Configure apk repo, add main and community channels.
```
# echo http://dl-cdn.alpinelinux.org/alpine/v3.20/main | sudo tee alpine-root/etc/apk/repositories
# echo http://dl-cdn.alpinelinux.org/alpine/v3.20/community | sudo tee -a alpine-root/etc/apk/repositories
```

3. Get into the virtual enviroment, *This command will be used next time after you exist from the virtual enviroment*
```
sudo chroot alpine-root /bin/sh --login
```

run the following commands to install the build tools
```
# apk update
# apk add openssh-server wpa_supplicant git dhcpcd dnsmasq u-boot-tools hostapd iptables avahi dbus chrony gpsd curl-dev htop frp jq libunwind zlib noip2 noip2-openrc netpbm musl-dev linux-headers g++ gcc cmake make minify fftw-dev fdk-aac-dev pkgconf perl gpsd-dev libunwind-dev zlib-dev sqlite-dev sqlite-static libconfig-static libconfig-dev patch automake autoconf
```

4. Inside the virtual enviroment, it is like a normal linux. You can use git to enlist the code, update submodules and use cmake to build the binary.
```
# cd /root
# git clone https://github.com/raspsdr/server
# cd server
# git submodule update --init
# mkdir build
# cd build
# cmake ..
# cmake --build .
```

5. Use the compiled binary websdr.bin to replace the one in the root of your TF card.

6. Happy hack

## Native browser harness

The native harness builds the server for the current Linux host, replaces the
Zynq hardware interface with deterministic fake receiver and waterfall data,
and opens the UI in headless Chromium.

Install the browser test dependency and Chromium once:

```sh
npm install
npx playwright install chromium
```

Run the complete build and browser smoke test:

```sh
npm run test:native-browser
```

Set `WEBSDR_HARNESS_PORT` to use a port other than 8073. The native harness
does not start GPS, update, registration, LED, or other target-only services.
DRM is excluded because its decoder processes depend on the target FDK-AAC
runtime.

## Modern web UI

The browser UI uses a compatibility-first design system layered over the
existing `w3_*` helpers. The implementation is intentionally framework-free so
the receiver, admin interface, and extensions keep their existing callback,
WebSocket, canvas, and embedded-asset behavior.

- `web/kiwi/modern_ui.css` defines semantic design tokens and the shared
  receiver, admin, extension, control, focus, and responsive styles.
- `web/kiwi/modern_ui.js` applies and persists the selected color theme.
- `web/kiwi/w3_util.js` emits stable `ui-*` component hooks while retaining the
  legacy W3.CSS classes used by existing callers and external extensions.
- `web/web.cpp` and `CMakeLists.txt` must both include any new UI asset so
  development loading and release embedding stay consistent.

The built-in themes are **Midnight** (default) and **Ember**.
They override semantic tokens through `data-ui-theme` on the document root.
Each uses neutral surfaces, one primary accent, and semantic status colors
instead of the legacy per-control rainbow palette. New themes should change
tokens instead of adding theme-specific component markup. Spectrum and
waterfall colormaps remain independent from the application theme.

When changing the UI:

1. Preserve existing element IDs, callback signatures, panel metadata, and
   canvas sizing behavior.
2. Prefer shared `ui-*` hooks or semantic tokens over page-specific color
   overrides.
3. Keep keyboard focus visible and respect `prefers-reduced-motion`.
4. Run `npm run test:native-browser`. The smoke test checks all themes plus
   desktop, tablet, phone portrait, and phone landscape receiver layouts, and
   desktop/tablet/phone admin layouts.
