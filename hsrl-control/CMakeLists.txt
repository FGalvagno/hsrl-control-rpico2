# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == NO EDITAR estas lineas, son para la extension de vs code ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ==============================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(hsrl-control C CXX ASM)

pico_sdk_init()

# ejecutable principal con los modulos separados
add_executable(hsrl-control
    main.c
    adc_capture.c
    seeder_comm.c
    control.c
)

pico_set_program_name(hsrl-control "hsrl-control")
pico_set_program_version(hsrl-control "0.2")

# programa pio para el led indicador
pico_generate_pio_header(hsrl-control ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# stdio por usb (la uart1 queda libre para el seeder)
pico_enable_stdio_uart(hsrl-control 0)
pico_enable_stdio_usb(hsrl-control 1)

target_include_directories(hsrl-control PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# librerias del sdk que usamos
target_link_libraries(hsrl-control
    pico_stdlib
    hardware_pio
    hardware_adc
    hardware_uart
)

pico_add_extra_outputs(hsrl-control)
