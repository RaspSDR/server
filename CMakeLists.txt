cmake_minimum_required(VERSION 3.13)
project(RaspSDR)

set(UPSTREAM_DIR ${CMAKE_SOURCE_DIR})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

find_program(MINIFY_COMMAND NAMES minify)

option(ZYNQ "Build for Zynq7000 serial based WebSDR" ON)
option(RPI "Build for RPI serial based WebSDR" OFF)

if(ZYNQ)
    set(ARCH zynq)
    set(CPU ARMA9)
    set(PLATFORM zynq)
    set(PLATFORM_INC ${PLATFORM})
    set(PLATOFRM_FLAGS -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard)
    FILE(GLOB PLATFORM_SRCS
        "${PLATFORM}/*.cpp"
    )
elseif(RPI)
    set(ARCH omap)
    set(CPU BCM2837)
    set(PLATFORM raspberrypi)
    set(PLATFORM_INC "${UPSTREAM_DIR}/platform/${PLATFORM}")
    FILE(GLOB PLATFORM_SRCS
        "${UPSTREAM_DIR}/platform/${PLATFORM}/*.cpp"
    )
else()
    set(PLATFORM simulator)
    SET(CPU X86_64)
    SET(ARCH Intel)
    set(PLATFORM_INC ${PLATFORM})
    FILE(GLOB PLATFORM_SRCS
        "${PLATFORM}/*.cpp"
    )
endif()

find_package(PkgConfig)
INCLUDE(GNUInstallDirs)

pkg_check_modules(FFTW3 REQUIRED fftw3f)
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(FDKAAC REQUIRED fdk-aac)

find_program(CCACHE_FOUND ccache)

if(CCACHE_FOUND)
    MESSAGE("CCache is Enabled")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif(CCACHE_FOUND)

# file(GLOB ASM_SRCS "${UPSTREAM_DIR}/e_cpu/asm/*.cpp")
# add_executable(asm ${ASM_SRCS})
# target_include_directories(asm PUBLIC "${UPSTREAM_DIR}/e_cpu/asm" "${UPSTREAM_DIR}/e_cpu")

# file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/gen)
# add_custom_target(kiwi_gen_h
#     COMMAND asm -o ${PROJECT_BINARY_DIR}/gen
#     WORKING_DIRECTORY "${UPSTREAM_DIR}/e_cpu/"
#     DEPENDS asm ${ASM_SRCS}
#     SOURCES ${ASM_SRCS}
# )

SET(EXT_SRCS ${PROJECT_BINARY_DIR}/extint.cpp ${UPSTREAM_DIR}/extensions/ext.cpp)

SET(EXTENSIONS
    ALE_2G
    colormap
    CW_decoder
    devl
    digi_modes
    DRM
    FAX
    FFT
    FSK
    FT8
    IBP_scan
    iframe
    IQ_display
    Loran_C
    NAVTEX
    noise_blank
    noise_filter
    S_meter
    s4285
    sig_gen
    SSTV
    timecode
    waterfall
    wspr
)

SET(GEN_EXT_MAIN "// auto-generated file -- do not edit by hand\n")
STRING(APPEND GEN_EXT_MAIN "void extint_init() {\n")

if(${CMAKE_C_COMPILER} MATCHES "clang")
    set(EXT_DEFINITIONS USE_CONSOLEIO USE_KIWI)
else()
    set(EXT_DEFINITIONS USE_CONSOLEIO USE_KIWI __UINT64_FMTx__="ll")
endif()

set(EXT_INCLUDES
    "${UPSTREAM_DIR}/extensions/FT8/ft8_lib"
    "${UPSTREAM_DIR}/extensions/DRM/dream/datadecoding"
    "${UPSTREAM_DIR}/extensions/DRM/dream/SDC"
)

FOREACH(ext_name ${EXTENSIONS})
    FILE(GLOB_RECURSE SRCS
        "${UPSTREAM_DIR}/extensions/${ext_name}/*.cpp"
    )
    FILE(GLOB_RECURSE C_SRCS
        "${UPSTREAM_DIR}/extensions/${ext_name}/*.c"
    )

    LIST(APPEND EXT_SRCS ${SRCS} ${C_SRCS})

    FOREACH(file_name ${SRCS})
        set_source_files_properties(${file_name} PROPERTIES COMPILE_DEFINITIONS "${EXT_DEFINITIONS}")
        set_source_files_properties(${file_name} PROPERTIES INCLUDE_DIRECTORIES "${EXT_INCLUDES}")
    ENDFOREACH()

    FOREACH(file_name ${C_SRCS})
        set_source_files_properties(${file_name} PROPERTIES COMPILE_DEFINITIONS "${EXT_DEFINITIONS}")
        set_source_files_properties(${file_name} PROPERTIES INCLUDE_DIRECTORIES "${EXT_INCLUDES}")
    ENDFOREACH()

    STRING(APPEND GEN_EXT_MAIN "\textern void ${ext_name}_main()\;\n${ext_name}_main()\;\n")
ENDFOREACH()

STRING(APPEND GEN_EXT_MAIN "}")
FILE(GENERATE
    OUTPUT ${PROJECT_BINARY_DIR}/extint.cpp
    CONTENT ${GEN_EXT_MAIN}
)

file(GLOB_RECURSE KIWI_SRCS
    ${PLATFORM_SRCS}
    "gps/*.cpp"
    "${UPSTREAM_DIR}/gps/sats.cpp"

    "${UPSTREAM_DIR}/main.cpp"
    "${UPSTREAM_DIR}/rx/*.cpp"
    "${UPSTREAM_DIR}/web/*.cpp"
    "${UPSTREAM_DIR}/net/*.cpp"
    "${UPSTREAM_DIR}/support/*.cpp"
    "${UPSTREAM_DIR}/ui/admin.cpp"
    "${UPSTREAM_DIR}/init/*.cpp"

    "${UPSTREAM_DIR}/pkgs/TNT_JAMA/*.cpp"
    "${UPSTREAM_DIR}/pkgs/jsmn/*.cpp"
    "${UPSTREAM_DIR}/pkgs/mongoose/*.cpp"
    "${UPSTREAM_DIR}/pkgs/sha256/*.cpp"
)

add_executable(kiwi.bin ${KIWI_SRCS} ${EXT_SRCS}
    ${CMAKE_BINARY_DIR}/edata_always.cpp ${CMAKE_BINARY_DIR}/edata_always2.cpp ${CMAKE_BINARY_DIR}/edata_embed.cpp
)
#add_dependencies(kiwi.bin kiwi_gen_h)

target_include_directories(kiwi.bin PUBLIC
    "."
    ${PLATFORM_INC}
    "gps"
    ${PROJECT_BINARY_DIR}/gen
    "${UPSTREAM_DIR}/"
    "${UPSTREAM_DIR}/support"
    "${UPSTREAM_DIR}/init"
    "${UPSTREAM_DIR}/rx"
    "${UPSTREAM_DIR}/web"
    "${UPSTREAM_DIR}/dev"
    "${UPSTREAM_DIR}/rx/CuteSDR"
    "${UPSTREAM_DIR}/rx/csdr"
    "${UPSTREAM_DIR}/rx/Teensy"
    "${UPSTREAM_DIR}/rx/kiwi"
    "${UPSTREAM_DIR}/rx/wdsp"
    "${UPSTREAM_DIR}/rx/CMSIS"
    "${UPSTREAM_DIR}/rx/fldigi/rsid"
    "${UPSTREAM_DIR}/rx/fldigi"
    "${UPSTREAM_DIR}/net"

    "${UPSTREAM_DIR}/extensions"
    "${UPSTREAM_DIR}/extensions/noise_blank"
    "${UPSTREAM_DIR}/extensions/wspr"
    "${UPSTREAM_DIR}/extensions/DRM"
    "${UPSTREAM_DIR}/extensions/FT8"
    "${UPSTREAM_DIR}/extensions/noise_filter"
    "${UPSTREAM_DIR}/extensions/DRM/dream"
    "${UPSTREAM_DIR}/extensions/DRM/dream/resample"

    "${UPSTREAM_DIR}/pkgs/TNT_JAMA"
    "${UPSTREAM_DIR}/pkgs/jsmn"
    "${UPSTREAM_DIR}/pkgs/mongoose"
    "${UPSTREAM_DIR}/pkgs/sha256"
    "${UPSTREAM_DIR}/pkgs/utf8/"
)

target_include_directories(kiwi.bin PUBLIC ${FFTW3_INCLUDE_DIRS})

target_link_directories(kiwi.bin PUBLIC
    ${GLFW3_LIBRARY_DIRS}
)

target_link_libraries(kiwi.bin PUBLIC
    ${FFTW3_STATIC_LIBRARIES}
    ${SNDFILE_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${FDKAAC_LIBRARIES}
    util
)

install(TARGETS kiwi.bin
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME kiwid
)

install(DIRECTORY ${PROJECT_BINARY_DIR}/htdoc
    DESTINATION "${CMAKE_INSTALL_DATADIR}/RaspSDR"
)

install(DIRECTORY ${UPSTREAM_DIR}/unix_env/kiwi.config/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/RaspSDR/config"
    PATTERN "Makefile" EXCLUDE
)

file(GLOB FIRMWARES ${UPSTREAM_DIR}/*SDR.rx*.bit)
install(FILES ${FIRMWARES}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/RaspSDR/firmware"
)

install(FILES ${UPSTREAM_DIR}/unix_env/kiwid
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/init.d"
)

install(FILES ${UPSTREAM_DIR}/unix_env/kiwid.service
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/systemd/system"
)

cmake_host_system_information(RESULT _host_name QUERY HOSTNAME)

set(DIR_DATA /tmp/kiwi.data)
set(ARCH_DIR arm32)

target_compile_definitions(kiwi.bin PUBLIC
    VERSION_MAJ=1 VERSION_MIN=248
    KIWI KIWISDR
    MULTI_CORE
    ARCH_${ARCH} CPU_${CPU} ARCH_CPU=${CPU} ARCH_CPU_S=STRINGIFY\(${CPU}\)
    DIR_CFG="/root/config"
    CFG_PREFIX=""
    ARCH_DIR=STRINGIFY\(${ARCH_DIR}\)
    DIR_DATA=STRINGIFY\(${CMAKE_INSTALL_FULL_DATAROOTDIR}\)
    REPO_GIT="THISHOULDNOTUSE"
    REPO_NAME="Beagle_SDR_GPS"
    BUILD_DIR="${PROJECT_BINARY_DIR}"
    COMPILE_HOST="${_host_name}"
    EDATA_EMBED

    # for extensions
    DRM HAVE_DLFCN_H HAVE_MEMORY_H HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRINGS_H
    HAVE_STRING_H STDC_HEADERS HAVE_INTTYPES_H HAVE_SYS_STAT_H
    HAVE_SYS_TYPES_H HAVE_UNISTD_H HAVE_LIBZ HAVE_LIBSNDFILE
    HAVE_LIBFDK_AAC HAVE_USAC
)

target_compile_options(kiwi.bin PUBLIC -O3 ${PLATOFRM_FLAGS} -Wno-fortify-source)

# ----------------------- Build FILES embeded
file(GLOB_RECURSE FILES_EMBED_JS RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/openwebrx/openwebrx.js
    ${UPSTREAM_DIR}/web/openwebrx/audio.js
    ${UPSTREAM_DIR}/web/openwebrx/ima_adpcm.js
    ${UPSTREAM_DIR}/web/extensions/ext.js

    ${UPSTREAM_DIR}/web/extensions/*.js
    ${UPSTREAM_DIR}/web/pkgs/js/*/*.js
    ${UPSTREAM_DIR}/web/pkgs/w3color.js

    ${UPSTREAM_DIR}/web/kiwi/kiwi.js
    ${UPSTREAM_DIR}/web/kiwi/kiwi_ui.js
    ${UPSTREAM_DIR}/web/kiwi/kiwi_util.js
    ${UPSTREAM_DIR}/web/kiwi/w3_util.js
    ${UPSTREAM_DIR}/web/kiwi/monitor.js
    ${UPSTREAM_DIR}/web/kiwi/kiwi_map.js

    ${UPSTREAM_DIR}/web/kiwi/admin.js
    ${UPSTREAM_DIR}/web/kiwi/admin_sdr.js
    ${UPSTREAM_DIR}/web/kiwi/mfg.js
    ${UPSTREAM_DIR}/web/kiwi/kiwi_js_load.js

    ${UPSTREAM_DIR}/web/pkgs/js/*.js
    ${UPSTREAM_DIR}/web/pkgs/js/*/*.js
)

file(GLOB_RECURSE FILES_EMBED_CSS RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/openwebrx/openwebrx.css
    ${UPSTREAM_DIR}/web/kiwi/kiwi.css
    ${UPSTREAM_DIR}/web/pkgs/w3.css
    ${UPSTREAM_DIR}/web/kiwi/w3_ext.css
    
    ${UPSTREAM_DIR}/web/extensions/*.css
    ${UPSTREAM_DIR}/web/pkgs/text-security/text-security-disc.css
)

file(GLOB FILES_EMBED_HTML RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/openwebrx/index.html
    ${UPSTREAM_DIR}/web/openwebrx/robots.txt
    ${UPSTREAM_DIR}/web/kiwi/ip_blacklist.default.cjson
    ${UPSTREAM_DIR}/web/kiwi/admin.html
    ${UPSTREAM_DIR}/web/kiwi/mfg.html
)

file(GLOB FILES_EMBED_MISC RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/extensions/ALE_2G/ALE_nets.cjson
    ${UPSTREAM_DIR}/web/extensions/DRM/stations.cjson
    ${UPSTREAM_DIR}/web/extensions/FAX/FAX_freq_menus.cjson
    ${UPSTREAM_DIR}/web/extensions/FSK/FSK_freq_menus.cjson
    ${UPSTREAM_DIR}/web/extensions/NAVTEX/NAVTEX_freq_menus.cjson
)

set(FILES_EMBED_FILES ${FILES_EMBED_JS} ${FILES_EMBED_CSS} ${FILES_EMBED_HTML} ${FILES_EMBED_MISC})

LIST(FILTER FILES_EMBED_FILES EXCLUDE REGEX ".*\\.min\\.js(\\.gz)?$")
LIST(FILTER FILES_EMBED_FILES EXCLUDE REGEX ".*\\.min\\.css(\\.gz)?$")
LIST(FILTER FILES_EMBED_FILES EXCLUDE REGEX ".*\\.min\\.html(\\.gz)?$")
LIST(FILTER FILES_EMBED_FILES EXCLUDE REGEX ".*Makefile$")

foreach(file ${FILES_EMBED_FILES})
    # string(REPLACE "openwebrx/" "" target_path "${file}")
    # string(REPLACE "kiwi/gfx/" "gfx/" target_path "${target_path}")
    set(target_path ${file})

    get_filename_component(FOLDER ${target_path} DIRECTORY)
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/htdoc/${FOLDER}")

    LIST(APPEND FILES_EMBED_FILES_OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path})
    if(${file} MATCHES "\\.css$")
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path}
            DEPENDS ${UPSTREAM_DIR}/web/${file}
            COMMAND uglifycss
                ${UPSTREAM_DIR}/web/${file} # Source file path
                > ${PROJECT_BINARY_DIR}/htdoc/${target_path} # Destination path in build directory
            COMMENT "Minify CSS ${file} to ${target_path}"
        )
    elseif(${file} MATCHES "\\.js$")
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path}
            DEPENDS ${UPSTREAM_DIR}/web/${file}
            COMMAND uglifyjs
                ${UPSTREAM_DIR}/web/${file} # Source file path
                > ${PROJECT_BINARY_DIR}/htdoc/${target_path} # Destination path in build directory
            COMMENT "Minify JS ${file} to ${target_path}"
        )
    elseif(${file} MATCHES "\\.html$")
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path}
            DEPENDS ${UPSTREAM_DIR}/web/${file}
            COMMAND ${MINIFY_COMMAND} --type html
                ${UPSTREAM_DIR}/web/${file} # Source file path
                -o ${PROJECT_BINARY_DIR}/htdoc/${target_path} # Destination path in build directory
            COMMENT "Minify html ${file} to ${target_path}"
        )
    else()
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path}
            DEPENDS ${UPSTREAM_DIR}/web/${file}
            COMMAND ${CMAKE_COMMAND} -E copy
                ${UPSTREAM_DIR}/web/${file} # Source file path
                ${PROJECT_BINARY_DIR}/htdoc/${target_path} # Destination path in build directory
            COMMENT "Copying ${file} to ${target_path}"
        )
    endif()
endforeach()

file(GLOB FILES_ALWAYS2 RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/kiwi/gfx/*.jpg
    ${UPSTREAM_DIR}/web/kiwi/gfx/*.png
    ${UPSTREAM_DIR}/web/kiwi/*.jpg
    ${UPSTREAM_DIR}/web/kiwi/*.png
    ${UPSTREAM_DIR}/web/openwebrx/gfx/*.jpg
    ${UPSTREAM_DIR}/web/openwebrx/gfx/*.png
    ${UPSTREAM_DIR}/web/openwebrx/icons/*.png
    ${UPSTREAM_DIR}/web/openwebrx/favicon.ico
    ${UPSTREAM_DIR}/web/openwebrx/gfx/font-expletus-sans/ExpletusSans-Medium.ttf
)

file(GLOB FILES_ALWAYS RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/pkgs/font-awesome/css/*
    ${UPSTREAM_DIR}/web/pkgs/font-awesome/fonts/*
    ${UPSTREAM_DIR}/web/pkgs/text-security/text-security-disc.eot
    ${UPSTREAM_DIR}/web/pkgs/text-security/text-security-disc.svg
    ${UPSTREAM_DIR}/web/pkgs/text-security/text-security-disc.ttf
    ${UPSTREAM_DIR}/web/pkgs/text-security/text-security-disc.woff
    ${UPSTREAM_DIR}/web/pkgs/font-awesome-4.6.3/css/font-awesome.min.css
)

list(SORT FILES_ALWAYS)
SET(FILES_ALWAYS_FULLPATH ${FILES_ALWAYS})
LIST(TRANSFORM FILES_ALWAYS_FULLPATH PREPEND ${UPSTREAM_DIR}/web/)
add_custom_command(
    COMMAND perl ${UPSTREAM_DIR}/web/mkdata.pl edata_always ${FILES_ALWAYS} > ${CMAKE_BINARY_DIR}/edata_always.cpp
    WORKING_DIRECTORY ${UPSTREAM_DIR}/web
    OUTPUT ${CMAKE_BINARY_DIR}/edata_always.cpp
    DEPENDS ${FILES_ALWAYS_FULLPATH}
    COMMENT "Add files into cpp"
)

list(SORT FILES_ALWAYS2)
SET(FILES_ALWAYS2_FULLPATH ${FILES_ALWAYS2})
LIST(TRANSFORM FILES_ALWAYS2_FULLPATH PREPEND ${UPSTREAM_DIR}/web/)
add_custom_command(
    COMMAND perl ${UPSTREAM_DIR}/web/mkdata.pl edata_always2 ${FILES_ALWAYS2} > ${CMAKE_BINARY_DIR}/edata_always2.cpp
    WORKING_DIRECTORY ${UPSTREAM_DIR}/web
    OUTPUT ${CMAKE_BINARY_DIR}/edata_always2.cpp
    DEPENDS ${FILES_ALWAYS2_FULLPATH}
    COMMENT "Add files into cpp"
)

list(SORT FILES_EMBED_FILES)
add_custom_command(
    COMMAND perl ${UPSTREAM_DIR}/web/mkdata.pl edata_embed ${FILES_EMBED_FILES} > ${CMAKE_BINARY_DIR}/edata_embed.cpp
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/htdoc
    OUTPUT ${CMAKE_BINARY_DIR}/edata_embed.cpp
    DEPENDS ${FILES_EMBED_FILES_OUTPUT}
    COMMENT "Add files into cpp"
)

# generate configs
file(GLOB_RECURSE config_FILES RELATIVE ${UPSTREAM_DIR}/unix_env
    ${UPSTREAM_DIR}/unix_env/kiwi.config/*.*
)

SET(FILES_CONFIG_OUTPUT ${config_FILES})
LIST(TRANSFORM FILES_CONFIG_OUTPUT PREPEND ${UPSTREAM_DIR}/unix_env/)
foreach(file ${config_FILES})
    string(REPLACE "kiwi.config/" "config/" target_path "${file}")
    string(REPLACE "dist." "" target_path "${target_path}")
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -E copy
            ${UPSTREAM_DIR}/unix_env/${file} # Source file path
            ${PROJECT_BINARY_DIR}/${target_path} # Destination path in build directory
        OUTPUT  ${PROJECT_BINARY_DIR}/${target_path}
        DEPENDS ${UPSTREAM_DIR}/unix_env/${file}
        COMMENT "Copying ${file} to build directory"
    )
endforeach()
add_custom_target(copy_config_target DEPENDS ${FILES_CONFIG_OUTPUT})

add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E copy
        ${UPSTREAM_DIR}/extensions/DRM/FDK-AAC/lib/arm32/libfdk-aac.so.2.0.1
        ${PROJECT_BINARY_DIR}/libfdk-aac.so
    OUTPUT ${PROJECT_BINARY_DIR}/libfdk-aac.so
    COMMENT "Copying libfdk-aac.so to build directory"
)
add_custom_target(copy_libfdk_target DEPENDS ${PROJECT_BINARY_DIR}/libfdk-aac.so)

add_dependencies(kiwi.bin copy_config_target)
add_dependencies(kiwi.bin copy_libfdk_target)

SET(CPACK_PACKAGE_NAME "ZynqSDR")
SET(CPACK_PACKAGE_VERSION_MAJOR 1)
SET(CPACK_PACKAGE_VERSION_MINOR 428)
SET(CPACK_PACKAGE_VERSION_PATCH 0)
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Howard Su")
SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Zynq7000 based single board WebSDR solution, which is derived from KiwiSDR")
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libfftw3-single3 curl wget avahi-daemon avahi-utils libnss-mdns avahi-autoipd miniupnpc dnsutils pnmtopng ethtool psmisc jq zlib1g libsndfile1 libfdk-aac")
INCLUDE(CPack)
