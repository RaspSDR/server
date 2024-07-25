# Web-888 Web Server Code

This code is forked from KiwiSDR project.

## The notable changes:

1. Use Linux Kernel Driver to interactive with hardware. No strict timing requirement on code.
1. Use Linux Pthread scheduler to replace userland task scheduler. Add lock protection in many code paths, which was assumed running in single core.
1. Use native thread instead of process for most blocking opertaions.
1. Use hardware GPS instead of Software Defined GPS receiver
1. Use PPS signal to tune ADC clock.
1. Disable HDL, ToDA extention for now
1. Use CMake as build system instead of Makefile, gcc as compiler instead of clang.

