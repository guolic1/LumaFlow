cmake_minimum_required(VERSION 3.22)

# Hand-maintained firmware build; CubeMX owns the project inside board/.

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME LumaFlow)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Build configurations: ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(board/cmake/stm32cubemx)

# Optimize across application and driver sources only in Release builds.
set_property(TARGET ${CMAKE_PROJECT_NAME} STM32_Drivers PROPERTY
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

# Select one entry point and linker script per build directory.
set(LUMAFLOW_IMAGE "application" CACHE STRING "Firmware image to build")
set_property(CACHE LUMAFLOW_IMAGE PROPERTY STRINGS application bootloader)
if(NOT LUMAFLOW_IMAGE STREQUAL "application" AND
   NOT LUMAFLOW_IMAGE STREQUAL "bootloader")
    message(FATAL_ERROR "LUMAFLOW_IMAGE must be application or bootloader")
endif()
message(STATUS "Firmware image: ${LUMAFLOW_IMAGE}")
add_subdirectory(${LUMAFLOW_IMAGE})

# Keep image layout out of the toolchain so it can be chosen per firmware target.
set(LUMAFLOW_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/${LUMAFLOW_IMAGE}/${LUMAFLOW_IMAGE}.ld")
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-T${LUMAFLOW_LINKER_SCRIPT}"
    "LINKER:-Map=$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.map"
)
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY
    LINK_DEPENDS "${LUMAFLOW_LINKER_SCRIPT}"
)
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY
    ADDITIONAL_CLEAN_FILES "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.map"
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)
