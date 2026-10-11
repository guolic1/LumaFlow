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
add_executable(fluid_sim_test fluid_sim_test.c ../application/fluid_sim.c)
target_include_directories(fluid_sim_test PRIVATE ../application)
if(MSVC)
    target_compile_options(fluid_sim_test PRIVATE /W4 /WX)
else()
    target_compile_options(fluid_sim_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME fluid_sim_test COMMAND fluid_sim_test)
add_executable(fluid_effect_test fluid_effect_test.c ../application/fluid_effect.c
    ../application/fluid_sim.c)
target_include_directories(fluid_effect_test PRIVATE ../application)
if(MSVC)
    target_compile_options(fluid_effect_test PRIVATE /W4 /WX)
else()
    target_compile_options(fluid_effect_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME fluid_effect_test COMMAND fluid_effect_test)
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
