# Hand-maintained toolchain for the top-level firmware build.
# CubeMX regenerates its own toolchains under board/cmake/.
set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
# Resolve the tools from local settings when present, otherwise from PATH.
set(TOOLCHAIN_PREFIX                arm-none-eabi-)
set(TOOLCHAIN_SUFFIX "")
if(CMAKE_HOST_WIN32)
    set(TOOLCHAIN_SUFFIX ".exe")
endif()

# Reuse local paths for CMake Tools and CLI builds; otherwise search PATH.
set(LUMAFLOW_LOCAL_SETTINGS "${CMAKE_CURRENT_LIST_DIR}/../.vscode/settings.json")
if(EXISTS "${LUMAFLOW_LOCAL_SETTINGS}")
    file(READ "${LUMAFLOW_LOCAL_SETTINGS}" LUMAFLOW_LOCAL_SETTINGS_JSON)
    string(JSON LUMAFLOW_LOCAL_SETTINGS_TYPE TYPE "${LUMAFLOW_LOCAL_SETTINGS_JSON}")
    string(JSON LUMAFLOW_ARM_PATH ERROR_VARIABLE LUMAFLOW_ARM_PATH_ERROR
        GET "${LUMAFLOW_LOCAL_SETTINGS_JSON}" "lumaflow.armToolchainPath")
    if(NOT LUMAFLOW_ARM_PATH_ERROR AND NOT LUMAFLOW_ARM_PATH STREQUAL "")
        file(TO_CMAKE_PATH "${LUMAFLOW_ARM_PATH}" LUMAFLOW_ARM_PATH)
        set(TOOLCHAIN_PREFIX "${LUMAFLOW_ARM_PATH}/arm-none-eabi-")
    endif()
    string(JSON LUMAFLOW_NINJA_PATH ERROR_VARIABLE LUMAFLOW_NINJA_PATH_ERROR
        GET "${LUMAFLOW_LOCAL_SETTINGS_JSON}" "lumaflow.ninjaPath")
    if(CMAKE_GENERATOR MATCHES "^Ninja" AND NOT CMAKE_MAKE_PROGRAM AND
       NOT LUMAFLOW_NINJA_PATH_ERROR AND NOT LUMAFLOW_NINJA_PATH STREQUAL "")
        set(CMAKE_MAKE_PROGRAM "${LUMAFLOW_NINJA_PATH}" CACHE FILEPATH "Ninja executable")
    endif()
endif()

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_SUFFIX})
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++${TOOLCHAIN_SUFFIX})
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++${TOOLCHAIN_SUFFIX})
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy${TOOLCHAIN_SUFFIX})
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size${TOOLCHAIN_SUFFIX})

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m0plus ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "m")
