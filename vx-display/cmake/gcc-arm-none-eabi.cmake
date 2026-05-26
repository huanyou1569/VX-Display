if(__TOOLCHAIN_LOADED)
    return()
endif()
set(__TOOLCHAIN_LOADED TRUE)

set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

# arm-none-eabi- must be in PATH
set(TOOLCHAIN_PREFIX                arm-none-eabi-)

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE   STATIC_LIBRARY)

# MCU-specific flags (Cortex-M7 with FPv5-D16 FPU)
set(TARGET_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard")

# C compiler flags (nano.specs only in linker flags — passing it here would duplicate at link time)
set(CMAKE_C_FLAGS "${TARGET_FLAGS} -Wall -fdata-sections -ffunction-sections"
    CACHE STRING "C compiler flags" FORCE)

# ASM compiler flags (no nano.specs — not applicable to assembler)
set(CMAKE_ASM_FLAGS "${TARGET_FLAGS} -x assembler-with-cpp -MMD -MP"
    CACHE STRING "ASM compiler flags" FORCE)

# Debug / Release optimization levels
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3"          CACHE STRING "C debug flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-Os -g0"         CACHE STRING "C release flags" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3"         CACHE STRING "CXX debug flags" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0"       CACHE STRING "CXX release flags" FORCE)

# C++ flags
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics"
    CACHE STRING "CXX compiler flags" FORCE)

# Linker flags (build string once, assign once)
set(_link_flags "${TARGET_FLAGS}")
set(_link_flags "${_link_flags} -T\"${CMAKE_SOURCE_DIR}/STM32H743XX_FLASH.ld\"")
set(_link_flags "${_link_flags} --specs=nano.specs")
set(_link_flags "${_link_flags} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(_link_flags "${_link_flags} -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS "${_link_flags}" CACHE STRING "Linker flags" FORCE)

set(TOOLCHAIN_LINK_LIBRARIES "m")
