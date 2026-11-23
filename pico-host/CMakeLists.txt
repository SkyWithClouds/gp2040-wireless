cmake_minimum_required(VERSION 3.20)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(bridge_pico_host C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
if(NOT TARGET tinyusb_pico_pio_usb)
  message(FATAL_ERROR "Provide the pinned Pico-PIO-USB checkout with PICO_PIO_USB_PATH")
endif()

# Match the pinned SDK's bool class-open ABI and poll the wired IN endpoint every frame.
# Patch a build-directory copy so repeated configuration leaves the checkout clean.
find_package(Patch REQUIRED)
configure_file(${TUSB_XINPUT_PATH}/xinput_host.c xinput_host.c COPYONLY)
set(xinput_patch ${CMAKE_CURRENT_SOURCE_DIR}/tusb_xinput.patch)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xinput_patch})
execute_process(COMMAND ${Patch_EXECUTABLE} -p1 -i ${xinput_patch}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
add_executable(pico-host src/main.c src/source.c src/usb_debug.c ${CMAKE_CURRENT_BINARY_DIR}/xinput_host.c)
target_include_directories(pico-host PRIVATE src ../common ${TUSB_XINPUT_PATH})
target_compile_options(pico-host PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>)
target_link_libraries(pico-host PRIVATE pico_stdlib hardware_spi pico_rand pico_unique_id
                      tinyusb_device tinyusb_host tinyusb_pico_pio_usb)
pico_enable_stdio_uart(pico-host 0)
pico_enable_stdio_usb(pico-host 0)
pico_add_extra_outputs(pico-host)
