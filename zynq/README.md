## Instruction to build/run on alpine

### Necessary Packages
apk add zlib libsndfile fftw-single-libs fdk-aac libopusenc curl
### Necessary Dev Packages
apk add minify zlib-dev libsndfile-dev libopusenc-dev fftw-dev fdk-aac-dev pkgconf cmake make 


## Cannot use alpine native fdk-aac lib 2.0.2
Crash as the following:
`
#0  0xb6d98af8 in CAacDecoder_Init(AAC_DECODER_INSTANCE*, CSAudioSpecificConfig const*, unsigned char, unsigned char*) () from target:/usr/lib/libfdk-aac.so.2
#1  0xb6d99c42 in aacDecoder_ConfigCallback(void*, CSAudioSpecificConfig const*, unsigned char, unsigned char*) () from target:/usr/lib/libfdk-aac.so.2
#2  0xb6dbb692 in transportDec_OutOfBandConfig(TRANSPORTDEC*, unsigned char*, unsigned int, unsigned int) () from target:/usr/lib/libfdk-aac.so.2
#3  0xb6d99e24 in aacDecoder_ConfigRaw () from target:/usr/lib/libfdk-aac.so.2
#4  0x004d1dfc in FdkAacCodec::DecOpen (this=0xb395f1d0, AudioParam=..., iAudioSampleRate=@0x2af3310: 45036304) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/sourcedecoders/fdk_aac_codec.cpp:258
#5  0x004cbda8 in CAudioSourceDecoder::InitInternal (this=0x2af153c <task_stacks+17936188>, Parameters=...) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/sourcedecoders/AudioSourceDecoder.cpp:406
#6  0x004791de in CModul<unsigned char, short>::InitThreadSave (this=this@entry=0x2af153c <task_stacks+17936188>, Parameter=...) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/MDI/../util/Modul.h:279
#7  0x0047073a in CModul<unsigned char, short>::Init (OutputBuffer=..., Parameter=..., this=0x2af153c <task_stacks+17936188>) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/MDI/../util/Modul.h:315
#8  CReceiverModul<unsigned char, short>::Init (OutputBuffer=..., Parameter=..., this=0x2af153c <task_stacks+17936188>) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/MDI/../util/Modul.h:540
#9  CReceiverModul<unsigned char, short>::ProcessData (OutputBuffer=..., InputBuffer=..., Parameter=..., this=0x2af153c <task_stacks+17936188>) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/MDI/../util/Modul.h:700
#10 CDRMReceiver::UtilizeDRM (this=0x2ae9f60 <task_stacks+17906016>, bEnoughData=@0x2ae9e23: true) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/DRMReceiver.cpp:372
#11 0x00470a94 in CDRMReceiver::process (this=this@entry=0x2ae9f60 <task_stacks+17906016>) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/DRMReceiver.cpp:977
#12 0x00480b9a in DRM_loop (rx_chan=rx_chan@entry=0) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/dream/DRM_main.cpp:145
#13 0x0045d3d0 in drm_task (param=0x0) at /root/ZynqSDR/Beagle_SDR_GPS/extensions/DRM/DRM.cpp:49
#14 0x00449f40 in trampoline (signo=<optimized out>) at /root/ZynqSDR/Beagle_SDR_GPS/support/coroutines.cpp:711
`

Or some errors like this:
`
Mon Feb 19 00:12:46 00:08:42.256 0.......            DRM AAC init
FDK DecOpen AAC+SBR-CRC channels_coded=0 coded_sample_rate=12000 channels=0 channel_config=1 sample_rate=0 extended_sample_rate=0 samples_per_frame=960 decoded_audio_frame_size=0 flags=9004d channel_0_type=0 index=0
DecOpen sample rate 12000
FDK zero output coded channels: err=0
FDK zero output channels: err=0
FDK Error condition is of unknown reason, or from a another module. Output buffer is invalid.
`