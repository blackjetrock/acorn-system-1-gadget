set(PROJECT acorn_system_1_gadget)
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_BUILD_TYPE Debug)

set(PICO_BOARD pimoroni_pico_plus2_rp2350 CACHE STRING "Board type")

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH    "~/pico/pico-sdk")
set(PICO_EXTRAS_PATH "~/pico-extras")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
project(acorn_system_1_gadget C CXX ASM)

set(PICO_COPY_TO_RAM 1)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)

add_compile_definitions(PICO_STACK_SIZE=10000)

add_compile_definitions(PICO_CORE1_STACK_SIZE=4000)
add_compile_definitions(PICO_USE_STACK_GUARDS=1)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# SD FAT LIB
#add_subdirectory(/tree/downloaded_tools/pico/ajm-no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)
add_subdirectory(../no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src build)

# Add executable. Default name is the project name, version 0.1

add_executable(acorn_system_1_gadget
  acorn_system_1_gadget.c
#  esc_desktop_display.c
  #fsms.c
  #esc_fsms.c
  #match_token.c
  oled.c
  sdcard.c
  hw_config.c
  #extracode.c
  #extracode_fp.c
  #extracode_basic.c
)

#add_library( gcc111libbid_hard UNKNOWN IMPORTED)
#SET_PROPERTY(TARGET gcc111libbid_hard  PROPERTY IMPORTED_LOCATION /tree/projects/github/fx201p-ram-replacement/firmware/acorn_system_1_gadget/build/gcc111libbid_hard.a)

#pico_generate_pio_header(acorn_system_1_gadget ${CMAKE_CURRENT_LIST_DIR}/picoputer.pio)

pico_set_program_name(acorn_system_1_gadget "acorn_system_1_gadget")
pico_set_program_version(acorn_system_1_gadget "0.1")

#pico_enable_stdio_uart(acorn_system_1_gadget 0)
pico_enable_stdio_usb(acorn_system_1_gadget 1)

# Add the standard library to the build
target_link_libraries(acorn_system_1_gadget pico_stdlib )

IF (NOT DEFINED N_SD_CARDS)
  SET(N_SD_CARDS 1)
ENDIF()

target_compile_definitions(acorn_system_1_gadget PUBLIC N_SD_CARDS=${N_SD_CARDS})

target_include_directories(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)


# Add the standard include files to the build
target_include_directories(${PROJECT} PRIVATE
                        .
                        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/source
                        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/sd_driver
                        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/include
                        # $ENV{PICO_SDK_PATH}/lib/tinyusb/hw
                        # $ENV{PICO_SDK_PATH}/lib/tinyusb/src
                        )


# Add any user requested libraries
target_link_libraries(acorn_system_1_gadget
 #gcc111libbid_hard
        #hardware_i2c
        hardware_pio
        hardware_clocks
	hardware_adc
	hardware_dma
        pico_sd_card
        pico_multicore
	#FatFs_SPI
        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
      )

pico_add_extra_outputs(acorn_system_1_gadget)

