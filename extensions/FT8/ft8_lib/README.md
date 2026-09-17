# FT8/FT4/FST4/FST4W library

C implementation of a lightweight FT8/FT4/FST4/FST4W decoder and encoder, mostly intended for experimental use on microcontrollers.

The intent of this library is to allow FT8/FT4/FST4/FST4W encoding and decoding in standalone environments (i.e. without a PC or RPi), e.g. automated beacons or SDR transceivers. It's also my learning process, optimization problem and source of fun.

The encoding process is relatively light on resources, and an Arduino should be perfectly capable of running this code.

The decoder is designed with memory and computing efficiency in mind, in order to be usable with a fast enough microcontroller. It is shown to be working on STM32F7 boards fast enough for real work, but the embedded application itself is beyond this repository. This repository provides an example decoder which can decode a 15-second WAV file on a desktop machine or SBC. The decoder needs to access the whole 15-second window in spectral magnitude representation (the window can be also shorter, and messages can have varying starting time within the window). The example FT8 decoder can work with slightly less than 200 KB of RAM.

**Above Statement is not true for this fork. The fork is focus on running this lib on a resonable microcontroller with Linux. The memory consumption is far more than 200KB and CPU usage is way more than the original version. I can run this version of decoder for 11 channels on Zynq7010 with 2 ARM cores.**

# Improvement over the original lib

|Change|Extra|Missed|Recall|Precision|F1 Score|
|---|---|---|----|-----|----|
|Improve regression tests, use wsjt-x result as baseline|14 (0.9%)| 480 (31.7%) | 68.3% | 98.2% |80.6%|
|Add OSD decoder |15 (1.0%)|466 (30.8%) | 69.2% | 97.9%| 81.1%|
|Increase time oversampling from 2 to 4| 18 (1.2%) | 442 (29.2%)|70.8%|97.7%|82.1%|
|Enable OSD deep decode|19 (1.3%)|436 (28.8%)| 71.2% | 97.5% | 82.3%
|substract encoded messages|20 (1.3%)|415 (27.4%)|72.6%|97.4%|83.2%
|two passes decode|24 (1.6%)|403 (26.6%)|73.4%|96.9%|83.5%
|Lower canidate threshold to 5|23 (1.5%)|402 (26.6%)|73.4%|97.0%|83.6%
|Increase # of canidate to 256|27 (1.8%)|391 (25.8%)|74.2%|96.5%|83.9%
|Modify substraction algorithm|24 (1.6%)|390 (25.8%)|74.2%|96.9%|84.1%
|Use float when computing snr|28 (1.8%)|386 (25.5%)|74.5%|96.4%|84.0%
|Substract in each freq_sub|34 (2.2%)|371 (24.5%)|75.5%|95.6%|84.4%
|three passes while reduce # of canidates per pass|31 (2.0%)|370 (24.4%)|75.6%|96.0%|84.6%
|Filter out fake signal via message formating check|27 (1.8%)|370 (24.4%)|75.6%|96.5%|84.8%
|Avoid decode the freq already decoded successfully (25% cpu saving)|24 (1.6%)|378 (25.0%)|75.0%|96.9%|84.6%

*** Assume all extra is fake spots to caculate F1 score, which is not necessary true. ***

>*Precision* avoids false positives
>
>Out of all the FT8 signals the decoder identified as valid decodes, how many were actually correct? (Measures how accurate decoding are.)

> *Recall* avoids false negatives
>
>Out of all the FT8 signals that were actually present, how many did the decoder successfully decode? (Measures how good decoder is at catching all signals.)

>*F1-Score*  balances above two metrics.
>
>A balance between precision and recall. It’s a single number that tells you how well the decoder performs overall in identifying FT8 signals.

# Current state

Currently the basic message set for establishing QSOs, as well as telemetry and free-text message modes are supported:
* CQ {call} {grid}, e.g. CQ CA0LL GG77
* CQ {xy} {call} {grid}, e.g. CQ JA CA0LL GG77
* {call} {call} {report}, e.g. CA0LL OT7ER R-07
* {call} {call} 73/RRR/RR73, e.g. OT7ER CA0LL 73
* Free-text messages (up to 13 characters from a limited alphabet) (decoding only, untested)
* Telemetry data (71 bits as 18 hex symbols)

Encoding and decoding works for FT8, FT4, FST4 and FST4W. For encoding and decoding, there is a console application provided for each, which serves mostly as test code, and could be a starting point for your potential application on an MCU. The console apps should run perfectly well on a RPi or a PC/Mac. I don't provide a concrete example for a particular MCU hardware here, since it would be very specific.

## FST4/FST4W Support

FST4 and FST4W are weak-signal digital protocols designed for LF and MF bands, supporting T/R periods of 15, 30, 60, 120, 300, 900, and 1800 seconds. Longer periods provide better sensitivity at the cost of QSO speed.

**Decoding a FST4 WAV file:**
```
./decode_ft8 -fst4 <period> <wav_file>
```
For example, to decode a 60-second FST4 recording:
```
./decode_ft8 -fst4 60 test/fst4/210115_0058.wav
```

**Decoding a FST4W WAV file:**
```
./decode_ft8 -fst4w <period> <wav_file>
```

**Encoding a FST4 message:**
```
./gen_ft8 -fst4 <period> "<message>" [frequency]
```

Key implementation details:
* FST4 uses a (240,101) LDPC code with 4-GFSK modulation and CRC-24 error detection
* FST4W uses a (240,74) LDPC code optimized for WSPR-like beacon operation
* The decoder uses a rectangular FFT window (vs Hann for FT8/FT4) since FST4 tone spacing equals the FFT bin width, providing orthogonal tone detection
* Tested against WSJT-X `fst4sim` signals from 0 to -20 dB SNR with 100% decode rate

### Memory-constrained integration

`monitor_get_memory_usage()` reports separate shared-DSP and per-waterfall-frame memory before initialization. `monitor_shared_init()` creates one FFT processor that can be used by multiple `monitor_frame_init()` waterfall frames. The original `monitor_init()` remains available for integrations that only need one self-contained monitor.

The memory-constrained web-888 profile uses:

- one shared FFT processor
- two rotating waterfall frames, so decoding can overlap capture of the next slot
- a 600-900 Hz analysis range matching web-888's existing 750 Hz BFO and 300 Hz WSPR filter
- 6 kHz monitor processing with exact 12 kHz-to-6 kHz decimation
- one streaming symbol buffer instead of retaining the complete T/R slot

`monitor_stream_process_i16()` accepts arbitrary 12 kHz chunks and converts them directly into monitor blocks. Its input must already be band-limited below the monitor sample rate's Nyquist frequency; web-888's 600-900 Hz frontend filter satisfies this requirement.

With time OSR 4 and frequency OSR 2, approximate FST4W memory is:

| T/R period | Memory/channel | Channels within 384 MiB |
|---:|---:|---:|
| 15 s | 0.1 MiB | 4174 |
| 30 s | 0.2 MiB | 2069 |
| 60 s | 0.4 MiB | 1008 |
| 120 s | 0.8 MiB | 499 |
| 300 s | 1.9 MiB | 197 |
| 900 s | 5.9 MiB | 65 |
| 1800 s | 11.8 MiB | 32 |

The FST4W-1800 profile is approximately 15 times smaller than the previous 177.8 MiB design. The 384 MiB limit reserves 128 MiB of a 512 MiB device for web-888 and other services. CPU capacity and existing receiver load will impose much lower practical channel counts than the memory-only figures.

The code is not yet really a library, rather a collection of routines and example code.

# Future ideas

Incremental decoding (processing during the 15 second window) is something that I would like to explore, but haven't started.

These features are low on my priority list:
* Contest modes
* Compound callsigns with country prefixes and special callsigns

# What to do with it

You can generate WAV files with your own messages as a proof of concept or for testing purposes. They can either be played back or opened directly from WSJT-X. To do that, run ```make```. Then run ```gen_ft8``` (run it without parameters to check what parameters are supported). Currently messages are modulated at 1000-1050 Hz.

You can decode WAV files with ```decode_ft8```. For FT8/FT4, use 15-second (or shorter) recordings. For FST4/FST4W, use recordings matching the T/R period (15s to 1800s). This is only an example application and does not support live processing/recording. For that you could use third party code (PortAudio, for example).

# References and credits

Thanks goes out to:
* my contributors who have provided me with various improvements which have often been beyond my skill set.
* Robert Morris, AB1HL, whose Python code (https://github.com/rtmrtmrtmrtm/weakmon) inspired this and helped to test various parts of the code.
* Mark Borgerding for his FFT implementation (https://github.com/mborgerding/kissfft). I have included a portion of his code.
* WSJT-X authors, who developed a very interesting and novel communications protocol

The details of FT4 and FT8 procotols and decoding/encoding are described here: https://physics.princeton.edu/pulsar/k1jt/FT4_FT8_QEX.pdf

The public part of FT4/FT8 implementation is included in this repository under ft4_ft8_public.

Of course in moments of frustration I have looked up the original WSJT-X code, which is mostly written in Fortran (http://physics.princeton.edu/pulsar/K1JT/wsjtx.html). However, this library contains my own original DSP routines and a different implementation of the decoder which is suitable for resource-constrained embedded environments.

Karlis Goba,
YL3JG
