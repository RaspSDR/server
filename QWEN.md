# Web-888 Server Codebase Context

## Project Overview

This project is the server component for the Web-888, a software-defined radio (SDR) receiver. It's a fork of the KiwiSDR project, significantly modified to interface with hardware via a Linux Kernel Driver, use Pthreads for scheduling, and integrate a hardware GPS with PPS signal for ADC clock tuning.

Key technologies and architecture:
- **Language**: C/C++
- **Build System**: CMake
- **Compiler**: gcc (Clang support seems to be conditional)
- **Concurrency**: Linux Pthreads
- **Web Framework**: Embedded Mongoose
- **Dependencies**: FFTW, zlib, fdk-aac, libgps, libunwind, SQLite, libconfig, libcurl, OpenSSL
- **Hardware**: Designed for ARM (specifically mentions Zynq7000), with build support for emulation on x86.

The server handles the core SDR functionality, including signal processing, web interface serving, network connections, and managing various SDR extensions.

## Building and Running

### Environment Setup
The project includes instructions for setting up a build environment using QEMU and Alpine Linux for ARM cross-compilation. This is the recommended approach as the code is targeted at ARM architecture.

### Build Process
1. Ensure all necessary packages are installed in your build environment (as listed in the README).
2. Clone the repository and initialize submodules:
   ```bash
   git clone https://github.com/raspsdr/server
   cd server
   git submodule update --init
   ```
3. Create a build directory and run CMake:
   ```bash
   mkdir build && cd build
   cmake ..
   ```
4. Build the project:
   ```bash
   cmake --build .
   ```
   This produces the main executable `websdr.bin`.

### Running
- The compiled binary `websdr.bin` is intended to replace the one on the target device's root filesystem (e.g., on a TF card).
- Specific runtime configuration and deployment instructions are not detailed in the explored files but would typically involve placing the binary and necessary configuration files in the correct locations on the target hardware.

## Development Conventions

- **Build System**: CMake is used extensively, with custom commands for embedding web resources (JS, CSS, HTML) into the binary, minifying them in release builds.
- **Code Structure**: The code is organized into directories like `rx` (receiver logic), `web` (web interface), `net` (networking), `gps`, `extensions`, etc. Extensions appear to be modular features (e.g., DRM, FT8, WSPR).
- **Concurrency**: Pthreads are used for concurrency, replacing the previous userland task scheduler.
- **Hardware Interface**: Interaction with hardware components is abstracted through a Linux Kernel Driver.
- **Memory Management**: Types are defined in `types.h` for fixed-width integers and other common data types.
- **Configuration**: Uses `libconfig` for configuration management.
- **Options**: Compile-time options are defined in `options.h`.