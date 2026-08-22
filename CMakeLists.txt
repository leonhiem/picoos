cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(picoos C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()

add_executable(picoos
    picoos.cpp
    buttons.cpp
    display.cpp
    heater.cpp
    shell.cpp
    jobs.cpp
    ntc.cpp
    dev/skintemp.cpp
    dev/lamp.cpp
    dev/buttons.cpp
    dev/seg7.cpp
    dev/alarm.cpp
    dev/current.cpp
    dev/heater.cpp
    dev/relay.cpp
    dev/setpoint.cpp
    dev/heaterauto.cpp
    dev/percent.cpp
    dev/pid.cpp
    dev/ambient.cpp
    dev/phase.cpp
    dev/alarmcheck.cpp
    dev/tft.cpp
    tft/st7735.c
    tft/gfx.c
    tft/icons.c
    tft/baby.c
    tft/heat_indicator.c
    tft/display.c
    prog/cat.cpp
    prog/echo.cpp
    prog/thresh.cpp
    prog/hyst.cpp
    prog/toggle.cpp
    prog/adjust.cpp
    prog/follow.cpp
    prog/pid.cpp
    prog/monitor.cpp
    prog/phase.cpp
    prog/safelut.cpp
    prog/select.cpp
    prog/alarmcheck.cpp
    prog/alarm.cpp
    prog/ledwire.cpp
    prog/tftwire.cpp
    prog/ls.cpp
)
add_subdirectory("../pico-onewire" pico_one_wire)
add_subdirectory("../pico-ads1115/lib" pico-ads1115)

# picoos_kernel: the reusable multitasking core. Kept as its own library
# target, separate from any one application's sources, so it can be lifted
# into future projects wholesale.
add_library(picoos_kernel
    kernel/task.cpp
    kernel/klog.cpp
    kernel/fs.cpp
    kernel/prog.cpp
)
target_include_directories(picoos_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(picoos_kernel pico_stdlib)

# blink_demo: step 1 — two tasks, two LEDs, proving the scheduler out in
# isolation before any warmer subsystem is migrated onto it.
add_executable(blink_demo
    demo_blink.cpp
)
target_link_libraries(blink_demo picoos_kernel pico_stdlib)
pico_enable_stdio_usb(blink_demo 1)
pico_enable_stdio_uart(blink_demo 0)
pico_add_extra_outputs(blink_demo)

# klog_demo: proves serialized cross-task logging (klog/klog_flush) before
# picoos.cpp's tasks are migrated onto it.
add_executable(klog_demo
    demo_klog.cpp
)
target_link_libraries(klog_demo picoos_kernel pico_stdlib)
pico_enable_stdio_usb(klog_demo 1)
pico_enable_stdio_uart(klog_demo 0)
pico_add_extra_outputs(klog_demo)

#add_library(LoRa_pico_lib ../LoRa-pi-pico/src/LoRa-RP2040.cpp ../LoRa-pi-pico/src/LoRa-RP2040.h)

target_include_directories(picoos PUBLIC)
target_link_directories(picoos PUBLIC)
target_link_libraries(picoos picoos_kernel pico_stdlib hardware_spi hardware_i2c hardware_interp hardware_adc hardware_gpio hardware_pwm pico_one_wire pico-ads1115 m)

# enable usb output, disable uart output
pico_enable_stdio_usb(picoos 1)
pico_enable_stdio_uart(picoos 1)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(picoos)

# example_auto_set_url(picoos)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
)

