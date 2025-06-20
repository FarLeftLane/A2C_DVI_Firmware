# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

# firmware version
set(FW_VERSION "1.7.6")
message(STATUS "Building firmware version ${FW_VERSION}")

option(FEATURE_PICO2 "Build project for PICO2 (RP2350) instead of original PICO (RP2040)" OFF)
option(FEATURE_TEST  "Build test firmware instead of normal firmware" OFF)
option(FEATURE_A2C  "Build A2C firmware instead of normal firmware" ON)

set(PICO_STDIO_UART OFF)
set(PICO_STDIO_USB  OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (FEATURE_PICO2)
    set(FAMILY rp2350)
    set(PICO_PLATFORM rp2350)
    set(FW_TYPE "_PICO2")
    set(A2DVI_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy_to_ram_custom_rp2350.ld)
    message(STATUS "Building for PICO2 (RP2350)...")
else()
    set(FAMILY rp2040)
    set(PICO_PLATFORM rp2040)
    set(FW_TYPE "")
    set(A2DVI_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy_to_ram_custom_rp2040.ld)
    message(STATUS "Building for PICO (RP2040)...")
endif()

if (FEATURE_A2C)
set(BINARY_NAME "A2C_DVI_v${FW_VERSION}${FW_TYPE}")
else()
set(BINARY_NAME "A2DVI_v${FW_VERSION}${FW_TYPE}")
endif()

if (FEATURE_A2C)
    message(STATUS "Building A2C version")
    add_compile_options(-DFEATURE_A2C)
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

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# include(./libraries/libdvi/CMakeLists.txt)

# Add executable. Default name is the project name, version 0.1

add_executable(${BINARY_NAME} 
    main.c
    applebus/abus.c
    applebus/abus_setup.c
    applebus/buffers.c

    a2c/a2c.c

    dvi/a2dvi.c
    dvi/tmds.c
    dvi/tmds_lores.c
    dvi/tmds_hires.c
    dvi/tmds_dhgr.c

    render/render.c
    render/render_splash.c
    render/render_debug.c
    render/render_text.c
    render/render_lores.c
    render/render_dgr.c
    render/render_hires.c
    render/render_dhgr.c
    render/render_videx.c

    config/config.c
    config/device_regs.c

    videx/videx_vterm.c

    menu/menu.c

    debug/debug.c
    util/dmacopy.c

    fonts/textfont.c
    fonts/iie_us_enhanced.c
    fonts/iie_us_unenhanced.c
    fonts/iie_us_reactive.c
    fonts/iie_uk_enhanced.c
    fonts/iie_fr_ca_enhanced.c
    fonts/iie_de_enhanced.c
    fonts/iie_spanish_enhanced.c
    fonts/iie_it_enhanced.c
    fonts/iie_hebrew_enhanced.c
    fonts/iie_se_fi_enhanced.c
    fonts/clone_pravetz_cyrillic.c
    fonts/iiplus_us.c
    fonts/iiplus_videx_lowercase1.c
    fonts/iiplus_videx_lowercase2.c
    fonts/iiplus_pigfont.c
    fonts/iiplus_jp_katakana.c

    fonts/videx/videx_normal.c
    fonts/videx/videx_inverse.c
    fonts/videx/videx_apl.c
    fonts/videx/videx_french.c
    fonts/videx/videx_super_sub.c
    fonts/videx/videx_uppercase.c
    fonts/videx/videx_epson.c
    fonts/videx/videx_german.c
    fonts/videx/videx_katakana.c
    fonts/videx/videx_spanish.c
    fonts/videx/videx_symbol.c

    ../libraries/libdvi/dvi.c
    ../libraries/libdvi/dvi.h
    ../libraries/libdvi/dvi_config_defs.h
    ../libraries/libdvi/dvi_serialiser.c
    ../libraries/libdvi/dvi_serialiser.h
    ../libraries/libdvi/dvi_timing.c
    ../libraries/libdvi/dvi_timing.h
    ../libraries/libdvi/tmds_encode.S
    ../libraries/libdvi/tmds_encode.c
    ../libraries/libdvi/tmds_encode.h
    ../libraries/libdvi/tmds_table.h
    ../libraries/libdvi/tmds_table_fullres.h
    ../libraries/libdvi/util_queue_u32_inline.h
    ../libraries/libdvi/audio_ring.c
    ../libraries/libdvi/audio_ring.h
    ../libraries/libdvi/data_packet.c
    ../libraries/libdvi/data_packet.h
     )

pico_set_program_name(${BINARY_NAME} "A2C_DVI")
pico_set_program_version(${BINARY_NAME} "1.7.1")

# Generate PIO header
# pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/applebus/abus.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../libraries/libdvi/dvi_serialiser.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../libraries/libdvi/tmds_encode_1bpp.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/a2c/a2c_SEROUT.pio)
pico_generate_pio_header(${BINARY_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/a2c/a2c_SND.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${BINARY_NAME} 0)
pico_enable_stdio_usb(${BINARY_NAME} 1)

# Add the standard library to the build
target_link_libraries(${BINARY_NAME}
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(${BINARY_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_include_directories(${BINARY_NAME} PRIVATE ./../libraries/libdvi)


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
        pico_stdlib
        )

# pico_add_extra_outputs(${BINARY_NAME})

pico_set_binary_type(${BINARY_NAME} copy_to_ram)

# create map/bin/hex file etc.
pico_add_extra_outputs(${BINARY_NAME})

# use platform-specific linker script
pico_set_linker_script(${BINARY_NAME} ${A2DVI_LINK_SCRIPT})

