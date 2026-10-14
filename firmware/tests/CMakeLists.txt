cmake_minimum_required(VERSION 3.22)

project(LumaFlowProtocolTests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(command_protocol_test
    command_protocol_test.c
    ../common/protocol/command_protocol.c
    ../common/protocol/common_commands.c
    ../application/commands.c
)
target_include_directories(command_protocol_test PRIVATE
    ../common/protocol
    ../application
    ../common/platform
)

add_executable(application_image_test
    application_image_test.c
    ../bootloader/application_image.c
)
target_include_directories(application_image_test PRIVATE
    ../bootloader
)

add_executable(led_matrix_scan_test
    led_matrix_scan_test.c
    ../application/led_matrix_scan.c
)
target_include_directories(led_matrix_scan_test PRIVATE ../application)

if(MSVC)
    target_compile_options(command_protocol_test PRIVATE /W4 /WX)
    target_compile_options(application_image_test PRIVATE /W4 /WX)
    target_compile_options(led_matrix_scan_test PRIVATE /W4 /WX)
else()
    target_compile_options(command_protocol_test PRIVATE -Wall -Wextra -Werror)
    target_compile_options(application_image_test PRIVATE -Wall -Wextra -Werror)
    target_compile_options(led_matrix_scan_test PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_executable(motion_sleep_test motion_sleep_test.c ../application/motion_sleep.c)
target_include_directories(motion_sleep_test PRIVATE ../application)
if(MSVC)
    target_compile_options(motion_sleep_test PRIVATE /W4 /WX)
else()
    target_compile_options(motion_sleep_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME motion_sleep_test COMMAND motion_sleep_test)

add_executable(fluid_sim_test fluid_sim_test.c ../application/fluid_sim.c)
target_include_directories(fluid_sim_test PRIVATE ../application)
# Exact trajectory assertions use calibrated coefficients; production tuning is
# exercised separately by fluid_effect_test and the coefficient variants below.
target_compile_definitions(fluid_sim_test PRIVATE FLUID_ACCEL_DIVISOR=32
    FLUID_VELOCITY_RETAIN_Q8=252 FLUID_WALL_RESTITUTION_Q8=32 FLUID_SURFACE_TENSION_Q8=3)
if(MSVC)
    target_compile_options(fluid_sim_test PRIVATE /W4 /WX)
else()
    target_compile_options(fluid_sim_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME fluid_sim_test COMMAND fluid_sim_test)
add_executable(fluid_effect_test fluid_effect_test.c ../application/fluid_effect.c
    ../application/fluid_sim.c ../application/motion_sleep.c)
target_include_directories(fluid_effect_test PRIVATE ../application)
if(MSVC)
    target_compile_options(fluid_effect_test PRIVATE /W4 /WX)
else()
    target_compile_options(fluid_effect_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME fluid_effect_test COMMAND fluid_effect_test)

# Exercise disabled effects and coefficient limits in independently compiled cores.
foreach(variant IN ITEMS fluid_controls_disabled_test fluid_controls_extreme_test)
    add_executable(${variant} fluid_controls_test.c ../application/fluid_sim.c
        ../application/fluid_effect.c)
    target_include_directories(${variant} PRIVATE ../application)
    if(MSVC)
        target_compile_options(${variant} PRIVATE /W4 /WX)
    else()
        target_compile_options(${variant} PRIVATE -Wall -Wextra -Werror)
    endif()
    target_compile_definitions(${variant} PRIVATE FLUID_ACCEL_DIVISOR=16U)
    add_test(NAME ${variant} COMMAND ${variant})
endforeach()
target_compile_definitions(fluid_controls_disabled_test PRIVATE
    FLUID_VELOCITY_RETAIN_Q8=256U FLUID_WALL_RESTITUTION_Q8=0U FLUID_SURFACE_TENSION_Q8=0U
    FLUID_INPUT_FILTER_Q8=0U FLUID_FLIP_Q8=128U FLUID_DENSITY_CORRECTION_DIVISOR=2U
    FLUID_RENDER_THRESHOLD=600 FLUID_RENDER_WEIGHT_DIVISOR=1024)
target_compile_definitions(fluid_controls_extreme_test PRIVATE
    FLUID_VELOCITY_RETAIN_Q8=0U FLUID_WALL_RESTITUTION_Q8=256U FLUID_SURFACE_TENSION_Q8=32U
    FLUID_SURFACE_TENSION_RADIUS=2048 FLUID_SEPARATION_PASSES=2
    FLUID_INPUT_FILTER_Q8=256U FLUID_FLIP_Q8=256U FLUID_DENSITY_CORRECTION_DIVISOR=1U)

add_executable(qma6100p_test qma6100p_test.c ../application/qma6100p.c)
target_include_directories(qma6100p_test PRIVATE ../application)
if(MSVC)
    target_compile_options(qma6100p_test PRIVATE /W4 /WX)
else()
    target_compile_options(qma6100p_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME qma6100p_test COMMAND qma6100p_test)
add_executable(imu_test imu_test.c ../application/imu.c ../application/qma6100p.c)
target_include_directories(imu_test PRIVATE ../application)
if(MSVC)
    target_compile_options(imu_test PRIVATE /W4 /WX)
else()
    target_compile_options(imu_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME imu_test COMMAND imu_test)
add_test(NAME command_protocol_test COMMAND command_protocol_test)
add_test(NAME application_image_test COMMAND application_image_test)
add_test(NAME led_matrix_scan_test COMMAND led_matrix_scan_test)
