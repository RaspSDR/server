cmake_minimum_required(VERSION 3.13)
project(RaspSDR)

include(ExternalProject)

set(UPSTREAM_DIR ${CMAKE_SOURCE_DIR})

find_program(MINIFY_COMMAND NAMES minify)

option(ZYNQ "Build for Zynq7000 serial based WebSDR" ON)
option(RPI "Build for RPI serial based WebSDR" OFF)
option(ENABLE_HDFL "Enable HFDL" OFF)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release)
   message(STATUS "Build type not specified: defaulting to Release")
endif(NOT CMAKE_BUILD_TYPE)

# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set (CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS "-Wall -fsingle-precision-constant -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3")

set(CMAKE_C_FLAGS "-Wall -fsingle-precision-constant")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-g -O3")


if(ZYNQ)
    set(PLATFORM zynq)
    set(PLATFORM_INC ${PLATFORM})
    set(PLATFORM_FLAGS -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math -fsingle-precision-constant -mvectorize-with-neon-quad)
    FILE(GLOB PLATFORM_SRCS
        "${PLATFORM}/*.cpp"
    )
else()
    # Not supported
    MESSAGE(FATAL "Not supported")
endif()

find_package(PkgConfig REQUIRED)
INCLUDE(GNUInstallDirs)

pkg_check_modules(FFTW3 REQUIRED fftw3f)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(FDKAAC REQUIRED fdk-aac)
pkg_check_modules(GPS REQUIRED libgps)
pkg_check_modules(UNWIND REQUIRED libunwind)
pkg_check_modules(SQLite3 REQUIRED sqlite3)
pkg_check_modules(CURL REQUIRED libcurl)

set(CONFIG_LIBRARIES "/usr/lib/libconfig++.a")

find_program(CCACHE_FOUND ccache)

if(CCACHE_FOUND)
    MESSAGE("CCache is Enabled")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif(CCACHE_FOUND)

if(ENABLE_HDFL)
    ## HFDL external depedency

    ExternalProject_Add(
        liquid-dsp
        GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp.git
        GIT_TAG        v1.6.0
        #PATCH_COMMAND  git apply ${CMAKE_SOURCE_DIR}/extensions/HFDL/patches/liquid.patch
        CONFIGURE_COMMAND cd <SOURCE_DIR> && ./bootstrap.sh && ./configure --prefix=${CMAKE_BINARY_DIR}/liquid-dsp
        BUILD_COMMAND make -j 8 -C <SOURCE_DIR>
        INSTALL_COMMAND make install -C <SOURCE_DIR>
    )

    set(LIQUID_DSP_INCLUDE_DIR ${CMAKE_BINARY_DIR}/liquid-dsp/include)
    set(LIQUID_DSP_LIB_DIR ${CMAKE_BINARY_DIR}/liquid-dsp/lib)

    ExternalProject_Add(
        libacars
        GIT_REPOSITORY https://github.com/szpajder/libacars
        GIT_TAG        v2.2.0
        #PATCH_COMMAND  git apply ${CMAKE_SOURCE_DIR}/extensions/HFDL/patches/libacars.patch
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/libacars
            -DBUILD_SHARED_LIBS=OFF  # Ensure static library is built
    )

    set(LIBACARS_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libacars/include)
    set(LIBACARS_LIB_DIR ${CMAKE_BINARY_DIR}/libacars/lib)

endif(ENABLE_HDFL)
###########################################################

SET(EXT_SRCS ${PROJECT_BINARY_DIR}/extint.cpp ${UPSTREAM_DIR}/extensions/ext.cpp)

SET(EXTENSIONS
    ALE_2G
    ant_switch
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
    SSTV
    timecode
    waterfall
    wspr
)

if(ENABLE_HDFL)
    list(APPEND EXTENSIONS HFDL)
endif(ENABLE_HDFL)

SET(GEN_EXT_MAIN "// auto-generated file -- do not edit by hand\n")
STRING(APPEND GEN_EXT_MAIN "void extint_init() {\n")

if(${CMAKE_C_COMPILER} MATCHES "clang")
    set(EXT_DEFINITIONS USE_CONSOLEIO USE_KIWI HFDL)
else()
    set(EXT_DEFINITIONS USE_CONSOLEIO USE_KIWI __UINT64_FMTx__="lld" HFDL)
endif()

set(EXT_INCLUDES
    "${UPSTREAM_DIR}/extensions/FT8/ft8_lib"
    "${UPSTREAM_DIR}/extensions/DRM/dream/datadecoding"
    "${UPSTREAM_DIR}/extensions/DRM/dream/SDC"
    ${LIQUID_DSP_INCLUDE_DIR}
    ${LIBACARS_INCLUDE_DIR}
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
    "${UPSTREAM_DIR}/si5351/*.cpp"

    "${UPSTREAM_DIR}/pkgs/mongoose/*.cpp"
    "${UPSTREAM_DIR}/pkgs/sha256/*.cpp"
)

add_executable(websdr.bin ${KIWI_SRCS} ${EXT_SRCS}
    ${CMAKE_BINARY_DIR}/edata_always.cpp ${CMAKE_BINARY_DIR}/edata_always2.cpp ${CMAKE_BINARY_DIR}/edata_embed.cpp
)
#add_dependencies(websdr.bin kiwi_gen_h)

target_include_directories(websdr.bin PUBLIC
    "."
    ${PLATFORM_INC}
    "gps"
    ${PROJECT_BINARY_DIR}
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

    "${UPSTREAM_DIR}/pkgs/jsmn"
    "${UPSTREAM_DIR}/pkgs/mongoose"
    "${UPSTREAM_DIR}/pkgs/sha256"
    "${UPSTREAM_DIR}/pkgs/utf8/"
)

target_include_directories(websdr.bin PUBLIC ${FFTW3_INCLUDE_DIRS})

target_link_directories(websdr.bin PUBLIC
    ${GLFW3_LIBRARY_DIRS}
)

target_link_libraries(websdr.bin PRIVATE
    libfftw3f.a libfftw3f_threads.a
    ${ZLIB_LIBRARIES}
    ${GPS_LIBRARIES}
    ${FDKAAC_LIBRARIES}
    ${UNWIND_LIBRARIES}
    ${CONFIG_LIBRARIES}
    ${CURL_LIBRARIES}
)

if(ENABLE_HDFL)
    target_link_libraries(websdr.bin PRIVATE
        ${LIQUID_DSP_LIB_DIR}/libliquid.a
        ${LIBACARS_LIB_DIR}/libacars-2.a
        /usr/lib/libsqlite3.a
    )
    add_dependencies(websdr.bin liquid-dsp libacars)

endif(ENABLE_HDFL)

cmake_host_system_information(RESULT _host_name QUERY HOSTNAME)
string(TIMESTAMP CURRENT_YEAR "%Y")
string(TIMESTAMP CURRENT_MONTH_DAY "%m%d")

# Check if the month starts with zero
if (${CURRENT_MONTH_DAY} MATCHES "^0[0-9].*")
    string(SUBSTRING ${CURRENT_MONTH_DAY} 1 3 CURRENT_MONTH_DAY)
endif()

set(DIR_DATA /tmp/kiwi.data)

target_compile_definitions(websdr.bin PUBLIC
    VERSION_MAJ=${CURRENT_YEAR} VERSION_MIN=${CURRENT_MONTH_DAY}
    KIWI KIWISDR
    DIR_CFG="/root/config"
    DIR_SAMPLES="/media/mmcblk0p1/config/samples"
    CFG_PREFIX=""
    DIR_DATA=STRINGIFY\(${DIR_DATA}\)
    BUILD_DIR="${PROJECT_BINARY_DIR}"
    COMPILE_HOST="${_host_name}"
    EDATA_EMBED

    # for CMSIS
    ARM_MATH_NEON
    ARM_MATH_LOOPUNROLL

    # for extensions
    DRM HAVE_DLFCN_H HAVE_MEMORY_H HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRINGS_H
    HAVE_STRING_H STDC_HEADERS HAVE_INTTYPES_H HAVE_SYS_STAT_H
    HAVE_SYS_TYPES_H HAVE_UNISTD_H HAVE_LIBZ
    HAVE_LIBFDK_AAC HAVE_USAC
)

target_compile_options(websdr.bin PUBLIC ${PLATFORM_FLAGS})

# ----------------------- Build FILES embeded
file(GLOB_RECURSE FILES_EMBED_JS RELATIVE ${UPSTREAM_DIR}/web
    ${UPSTREAM_DIR}/web/openwebrx/openwebrx.js
    ${UPSTREAM_DIR}/web/openwebrx/audio.js
    ${UPSTREAM_DIR}/web/openwebrx/ima_adpcm.js
    ${UPSTREAM_DIR}/web/extensions/ext.js

    ${UPSTREAM_DIR}/web/extensions/*.js
    ${UPSTREAM_DIR}/web/pkgs/js/*/*.js
    ${UPSTREAM_DIR}/web/pkgs/w3/w3color.js

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
    ${UPSTREAM_DIR}/web/pkgs/w3/w3.css
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
    ${UPSTREAM_DIR}/web/extensions/HFDL/systable.backup.cjson
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
    if(${file} MATCHES "\\.css$" AND NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path}
            DEPENDS ${UPSTREAM_DIR}/web/${file}
            COMMAND ${MINIFY_COMMAND} --type css
                ${UPSTREAM_DIR}/web/${file} # Source file path
                -o ${PROJECT_BINARY_DIR}/htdoc/${target_path} # Destination path in build directory
            COMMENT "Minify CSS ${file} to ${target_path}"
        )
    elseif(${file} MATCHES "\\.js$" AND NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/htdoc/${target_path}
            DEPENDS ${UPSTREAM_DIR}/web/${file}
            COMMAND ${MINIFY_COMMAND} --type js
                ${UPSTREAM_DIR}/web/${file} # Source file path
                -o ${PROJECT_BINARY_DIR}/htdoc/${target_path} # Destination path in build directory
            COMMENT "Minify JS ${file} to ${target_path}"
        )
    elseif(${file} MATCHES "\\.html$" AND NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
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

foreach(file ${config_FILES})
    string(REPLACE "kiwi.config/" "config/" target_path "${file}")
    string(REPLACE "dist." "" target_path "${target_path}")
    LIST(APPEND FILES_CONFIG_OUTPUT ${PROJECT_BINARY_DIR}/${target_path})
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

add_dependencies(websdr.bin copy_config_target)
add_dependencies(websdr.bin copy_libfdk_target)

# Function to download the file only if it's outdated or doesn't exist
function(download_if_needed url local_file)
    # Check if the local file exists
    if(EXISTS "${local_file}")
        # Get the local file's modification time
        file(TIMESTAMP "${local_file}" LOCAL_TIMESTAMP)
    else()
        # If it doesn't exist, set LOCAL_TIMESTAMP to a very old time
        set(LOCAL_TIMESTAMP 0)
    endif()

    # Fetch the remote file's modification time
    file(DOWNLOAD "${url}" "${local_file}.tmp" STATUS status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Failed to download file: ${status}")
    endif()

    file(TIMESTAMP "${local_file}.tmp" REMOTE_TIMESTAMP)

    # Compare timestamps
    if(REMOTE_TIMESTAMP GREATER LOCAL_TIMESTAMP)
        # Only rename if the remote file is newer
        file(RENAME "${local_file}.tmp" "${local_file}")
        message(STATUS "Downloaded new version of the file: ${local_file}")
    else()
        # Clean up the temporary file
        file(REMOVE "${local_file}.tmp")
        message(STATUS "Local file is up to date: ${local_file}")
    endif()
endfunction()

download_if_needed("http://www.eibispace.de/dx/sked-b24.csv" ${PROJECT_BINARY_DIR}/sked-current.csv)

add_executable(eibi_proc ${UPSTREAM_DIR}/pkgs/EiBi/eibi_proc.cpp)
target_include_directories(eibi_proc PUBLIC
    "."
    ${PLATFORM_INC}
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

    "${UPSTREAM_DIR}/pkgs/jsmn"
    "${UPSTREAM_DIR}/pkgs/mongoose"
    "${UPSTREAM_DIR}/pkgs/sha256"
    "${UPSTREAM_DIR}/pkgs/utf8/"
)

add_custom_command(
    COMMAND ${PROJECT_BINARY_DIR}/eibi_proc ${PROJECT_BINARY_DIR}/sked-current.csv ${PROJECT_BINARY_DIR}/EiBi.h
    WORKING_DIRECTORY ${UPSTREAM_DIR}
    OUTPUT ${PROJECT_BINARY_DIR}/EiBi.h
    DEPENDS ${PROJECT_BINARY_DIR}/sked-current.csv eibi_proc
    COMMENT "Add files into cpp"
)
add_custom_target(eibi_gen_target DEPENDS ${PROJECT_BINARY_DIR}/EiBi.h)
add_dependencies(websdr.bin eibi_gen_target)