cmake_minimum_required(VERSION 3.18)

# firmware version
set(FW_VERSION "1.8.1a")
message(STATUS "Building firmware version ${FW_VERSION}")

option(FEATURE_PICO2 "Build project for PICO2 (RP2350) instead of original PICO (RP2040)" OFF)
option(FEATURE_TEST  "Build test firmware instead of normal firmware" OFF)
option(FEATURE_A2C  "Build A2C firmware instead of normal A2DVI slotted firmware" OFF)
option(FEATURE_A2_AUDIO  "Experimental Audio support" ON)

set(PICO_STDIO_UART OFF)
set(PICO_STDIO_USB  OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (FEATURE_PICO2)
    set(FAMILY rp2350)
    set(PICO_PLATFORM rp2350)
    set(FW_TYPE "_PICO2")
    set(A2DVI_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/firmware/scripts/copy_to_ram_custom_rp2350.ld)
    message(STATUS "Building for PICO2 (RP2350)...")
else()
    set(FAMILY rp2040)
    set(PICO_PLATFORM rp2040)
    set(FW_TYPE "")
if (FEATURE_A2C)
    set(A2DVI_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/firmware/scripts/copy_to_ram_custom_rp2040_a2c.ld)
else()
    set(A2DVI_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/firmware/scripts/copy_to_ram_custom_rp2040.ld)
endif()

    message(STATUS "Building for PICO (RP2040)...")
endif()

if (FEATURE_A2C)
if (FEATURE_A2_AUDIO)
set(BINARY_NAME "A2C_DVI_S_v${FW_VERSION}${FW_TYPE}")
else()
set(BINARY_NAME "A2DVI_v${FW_VERSION}${FW_TYPE}")
endif()
else()
set(BINARY_NAME "A2DVI_v${FW_VERSION}${FW_TYPE}")
endif()

if (FEATURE_A2C)
    message(STATUS "Building A2C version")
    add_compile_options(-DFEATURE_A2C)
endif()

if (FEATURE_A2_AUDIO)
    message(STATUS "Building A2 audio version")
    add_compile_options(-DFEATURE_A2_AUDIO)
endif()

if (FEATURE_TEST)
    message(STATUS "Building TEST version")
    add_compile_options(-DFEATURE_TEST)
    set(BINARY_NAME "${BINARY_NAME}_TEST")
    set(TEST_SOURCES "test/tests.c")
else()
    message(STATUS "Building Release version")
endif()

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")
# set(BOARD pico_sdk)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(${BINARY_NAME} C CXX ASM)

# enable compiler warnings
add_compile_options(-Wall)

# At 640pixels each TMDS buffer requires 3840bytes
add_compile_options(-DDVI_N_TMDS_BUFFERS=8)

add_compile_options(-O2)

add_compile_options(-DFW_VERSION="${FW_VERSION}")

# wrap/disable some library symbols, to speed up the boot time (reduce code)
add_link_options(-Wl,--wrap=weak_raw_vprintf)
add_link_options(-Wl,--wrap=panic)

# just for debugging
#add_compile_options(-DFEATURE_DEBUG_COUNTER)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# include(../libraries/libdvi/CMakeLists.txt)

# Add executable. Default name is the project name, version 0.1
if (FEATURE_A2_AUDIO)
add_executable(${BINARY_NAME} 
    firmware/main.c
    firmware/applebus/abus.c
    firmware/applebus/abus_setup.c
    firmware/applebus/buffers.c

#    firmware/a2c/a2c.c

    firmware/dvi/a2dvi.c
    firmware/dvi/tmds.c
    firmware/dvi/tmds_lores.c
    firmware/dvi/tmds_hires.c
    firmware/dvi/tmds_dhgr.c

    firmware/render/render.c
    firmware/render/render_splash.c
    firmware/render/render_debug.c
    firmware/render/render_text.c
    firmware/render/render_lores.c
    firmware/render/render_dgr.c
    firmware/render/render_hires.c
    firmware/render/render_dhgr.c
    firmware/render/render_videx.c

    firmware/config/config.c
    firmware/config/device_regs.c

    firmware/videx/videx_vterm.c

    firmware/menu/menu.c

    firmware/debug/debug.c
    firmware/util/dmacopy.c

    firmware/fonts/textfont.c
    firmware/fonts/iie_us_enhanced.c
    firmware/fonts/iie_us_unenhanced.c
    firmware/fonts/iie_us_reactive.c
    firmware/fonts/iie_uk_enhanced.c
    firmware/fonts/iie_fr_ca_enhanced.c
    firmware/fonts/iie_de_enhanced.c
    firmware/fonts/iie_spanish_enhanced.c
    firmware/fonts/iie_it_enhanced.c
    firmware/fonts/iie_hebrew_enhanced.c
    firmware/fonts/iie_se_fi_enhanced.c
    firmware/fonts/clone_pravetz_cyrillic.c
    firmware/fonts/iiplus_us.c
    firmware/fonts/iiplus_videx_lowercase1.c
    firmware/fonts/iiplus_videx_lowercase2.c
    firmware/fonts/iiplus_pigfont.c
    firmware/fonts/iiplus_jp_katakana.c

    firmware/fonts/videx/videx_normal.c
    firmware/fonts/videx/videx_inverse.c
    firmware/fonts/videx/videx_apl.c
    firmware/fonts/videx/videx_french.c
    firmware/fonts/videx/videx_super_sub.c
    firmware/fonts/videx/videx_uppercase.c
    firmware/fonts/videx/videx_epson.c
    firmware/fonts/videx/videx_german.c
    firmware/fonts/videx/videx_katakana.c
    firmware/fonts/videx/videx_spanish.c
    firmware/fonts/videx/videx_symbol.c

    libraries/libdvi/dvi.c
    libraries/libdvi/dvi.h
    libraries/libdvi/dvi_config_defs.h
    libraries/libdvi/dvi_serialiser.c
    libraries/libdvi/dvi_serialiser.h
    libraries/libdvi/dvi_timing.c
    libraries/libdvi/dvi_timing.h
    libraries/libdvi/tmds_encode.S
    libraries/libdvi/tmds_encode.c
    libraries/libdvi/tmds_encode.h
    libraries/libdvi/tmds_table.h
    libraries/libdvi/tmds_table_fullres.h
    libraries/libdvi/util_queue_u32_inline.h

    libraries/libdvi/audio_ring.h
    libraries/libdvi/data_packet.c
    libraries/libdvi/data_packet.h
    )
else()
add_executable(${BINARY_NAME} 
    firmware/main.c
    firmware/applebus/abus.c
    firmware/applebus/abus_setup.c
    firmware/applebus/buffers.c

    firmware/a2c/a2c.c

    firmware/dvi/a2dvi.c
    firmware/dvi/tmds.c
    firmware/dvi/tmds_lores.c
    firmware/dvi/tmds_hires.c
    firmware/dvi/tmds_dhgr.c

    firmware/render/render.c
    firmware/render/render_splash.c
    firmware/render/render_debug.c
    firmware/render/render_text.c
    firmware/render/render_lores.c
    firmware/render/render_dgr.c
    firmware/render/render_hires.c
    firmware/render/render_dhgr.c
    firmware/render/render_videx.c

    firmware/config/config.c
    firmware/config/device_regs.c

    firmware/videx/videx_vterm.c

    firmware/menu/menu.c

    firmware/debug/debug.c
    firmware/util/dmacopy.c

    firmware/fonts/textfont.c
    firmware/fonts/iie_us_enhanced.c
    firmware/fonts/iie_us_unenhanced.c
    firmware/fonts/iie_us_reactive.c
    firmware/fonts/iie_uk_enhanced.c
    firmware/fonts/iie_fr_ca_enhanced.c
    firmware/fonts/iie_de_enhanced.c
    firmware/fonts/iie_spanish_enhanced.c
    firmware/fonts/iie_it_enhanced.c
    firmware/fonts/iie_hebrew_enhanced.c
    firmware/fonts/iie_se_fi_enhanced.c
    firmware/fonts/clone_pravetz_cyrillic.c
    firmware/fonts/iiplus_us.c
    firmware/fonts/iiplus_videx_lowercase1.c
    firmware/fonts/iiplus_videx_lowercase2.c
    firmware/fonts/iiplus_pigfont.c
    firmware/fonts/iiplus_jp_katakana.c

    firmware/fonts/videx/videx_normal.c
    firmware/fonts/videx/videx_inverse.c
    firmware/fonts/videx/videx_apl.c
    firmware/fonts/videx/videx_french.c
    firmware/fonts/videx/videx_super_sub.c
    firmware/fonts/videx/videx_uppercase.c
    firmware/fonts/videx/videx_epson.c
    firmware/fonts/videx/videx_german.c
    firmware/fonts/videx/videx_katakana.c
    firmware/fonts/videx/videx_spanish.c
    firmware/fonts/videx/videx_symbol.c

    libraries/libdvi/dvi.c
    libraries/libdvi/dvi.h
    libraries/libdvi/dvi_config_defs.h
    libraries/libdvi/dvi_serialiser.c
    libraries/libdvi/dvi_serialiser.h
    libraries/libdvi/dvi_timing.c
    libraries/libdvi/dvi_timing.h
    libraries/libdvi/tmds_encode.S
    libraries/libdvi/tmds_encode.c
    libraries/libdvi/tmds_encode.h
    libraries/libdvi/tmds_table.h
    libraries/libdvi/tmds_table_fullres.h
    libraries/libdvi/util_queue_u32_inline.h
     )
endif()

pico_set_program_name(${BINARY_NAME} "A2DVI")
pico_set_program_version(${BINARY_NAME} "${FW_VERSION}")

# Generate PIO header
# pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/firmware/applebus/abus.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/libraries/libdvi/dvi_serialiser.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/libraries/libdvi/tmds_encode_1bpp.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/firmware/a2c/a2c_SEROUT.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${BINARY_NAME} 0)
pico_enable_stdio_usb(${BINARY_NAME} 1)

# Add the standard library to the build
target_link_libraries(${BINARY_NAME}
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(${BINARY_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/firmware
)

target_include_directories(${BINARY_NAME} PRIVATE libraries/libdvi)

# Add any user requested libraries
target_link_libraries(${BINARY_NAME} 
        pico_base_headers
        pico_runtime_init
        pico_runtime
        pico_multicore
        pico_util
        hardware_flash
        hardware_vreg
        hardware_clocks
        hardware_dma
        hardware_pio
        hardware_interp
        hardware_pwm
        hardware_adc
        pico_stdlib
        )

#target_include_directories(${BINARY_NAME} PUBLIC lib/PicoDVI/software/include)
#target_include_directories(${BINARY_NAME} PUBLIC assets .)

pico_set_binary_type(${BINARY_NAME} copy_to_ram)

# create map/bin/hex file etc.
pico_add_extra_outputs(${BINARY_NAME})

# use platform-specific linker script
pico_set_linker_script(${BINARY_NAME} ${A2DVI_LINK_SCRIPT})

