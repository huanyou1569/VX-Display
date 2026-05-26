cmake_minimum_required(VERSION 3.22)

# Enable compile commands for clangd indexing
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project definition
project(vx-display C ASM)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Executable target
add_executable(${PROJECT_NAME})

# STM32CubeMX generated code (HAL drivers, CMSIS, startup, application sources)
add_subdirectory(cmake/stm32cubemx)

# ============================================================
# User code sources
# ============================================================
set(USER_CODE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Core/user_code)

target_sources(${PROJECT_NAME} PRIVATE
    # LED / WS2812 driver
    ${USER_CODE_DIR}/led_buffer.c
    ${USER_CODE_DIR}/ws2812_code.c
    ${USER_CODE_DIR}/ws2812_enable.c
    ${USER_CODE_DIR}/ws2812_gpio.c
    ${USER_CODE_DIR}/ws2812_port.c
    ${USER_CODE_DIR}/ws2812_driver.c

    # Volume rendering / math
    ${USER_CODE_DIR}/volume_math.c
    ${USER_CODE_DIR}/volume_buffer.c
    ${USER_CODE_DIR}/volume_draw.c
    ${USER_CODE_DIR}/volume_to_ws2812.c
    ${USER_CODE_DIR}/volume_hall.c

    # Motor control
    ${USER_CODE_DIR}/motor_control.c

    # SD card animation loader
    ${USER_CODE_DIR}/sd_animation.c

    # Bluetooth command handler
    ${USER_CODE_DIR}/bt_commands.c

    # Pre-computed slice player
    ${USER_CODE_DIR}/slice_player.c

    # Animation player (dual-buffered)
    ${USER_CODE_DIR}/anim_player.c

    # Animation player V2 (1-bit + 50-slice + 180° symmetry)
    ${USER_CODE_DIR}/anim_player_v2.c

    # Animation player V3 (6-bit 64-color + double buffer + async SD)
    ${USER_CODE_DIR}/anim_player_v3.c

    # Interaction sphere (particle-based + joystick input)
    ${USER_CODE_DIR}/interaction_sphere.c
    # 3D brush / paint mode (canvas + cursor)
    ${USER_CODE_DIR}/brush_mode.c
    # 3D snake game
    ${USER_CODE_DIR}/snake_game.c
)

# ============================================================
# Include paths
# ============================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${USER_CODE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/model_data
)

# ============================================================
# Link libraries
# ============================================================
target_link_libraries(${PROJECT_NAME}
    stm32cubemx
)

# Remove libob.a dependency (not available on bare-metal ARM)
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)
