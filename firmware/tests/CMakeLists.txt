cmake_minimum_required(VERSION 3.22)

project(LumaFlowProtocolTests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(command_protocol_test
    command_protocol_test.c
    ../common/protocol/command_protocol.c
    ../common/protocol/common_commands.c
)
target_include_directories(command_protocol_test PRIVATE
    ../common/protocol
)

if(MSVC)
    target_compile_options(command_protocol_test PRIVATE /W4 /WX)
else()
    target_compile_options(command_protocol_test PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME command_protocol_test COMMAND command_protocol_test)
