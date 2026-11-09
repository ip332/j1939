cmake_minimum_required(VERSION 3.16)

project(J1939)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(ENABLE_COVERAGE "Build with gcov instrumentation for coverage reports" OFF)
if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -O0 -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Standard macro to build a STATIC library.
macro(define_lib LIB_NAME)
    cmake_minimum_required(VERSION 3.10)
    project (${LIB_NAME})
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Werror -Wall -Wextra -Wno-unused-parameter")
    set (CMAKE_INCLUDE_CURRENT_DIR ON)
    file(GLOB SRCS "*.cpp")
    # Test files are colocated with the code they test (see tests/*_test(s).cpp moved
    # into each module's own directory) but must never end up compiled into the
    # production library itself.
    list(FILTER SRCS EXCLUDE REGEX ".*_tests?\\.cpp$")
    file(GLOB HDRS "*.h")
    list(APPEND PUBLIC_HEADERS ${HDRS})
    set (PUBLIC_HEADERS  ${PUBLIC_HEADERS} PARENT_SCOPE)
    add_library(${LIB_NAME} SHARED ${HDRS} ${SRCS} ${ARGN})
endmacro()

# List all subfolders which generate static libs
list(APPEND TARGETS
        j1939_parser
        can_stream
        can_reader)

include_directories(options)
include_directories(j1939_parser/dbc_parser)

foreach(target ${TARGETS})
    include_directories(${target})
endforeach()

foreach(target ${TARGETS})
    add_subdirectory(${target})
endforeach()

include_directories(can_sandbox_lib)
add_subdirectory(can_sandbox_lib)

# Console application
add_subdirectory(console)

# DBC compiler
add_executable(dbc_compiler dbc_compiler/dbc_compiler.cpp)
target_link_libraries(dbc_compiler j1939_parser)
install(TARGETS dbc_compiler RUNTIME DESTINATION bin)

# Enable unit tests in a standalone build or when UNIT_TEST was added to the bitbake recipe.
find_package(GTest)

add_definitions(-DDBC_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/tests/dbc/")
# Sample CAN logs: committed excerpts in testdata/canboat/, and the full captures that
# scripts/fetch_test_logs.sh downloads into testdata/external/ (see testdata/README.md).
add_definitions(-DTESTDATA_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/testdata/")

# Build test app. Test files are colocated with the code they test rather than all
# living in tests/, so collect them from each module's own directory.
set(TEST_SOURCE_DIRS
    tests
    can_stream
    can_reader
    can_sandbox_lib
    j1939_parser
    j1939_parser/dbc_parser
    options)
set(ALL_TESTS)
foreach(dir ${TEST_SOURCE_DIRS})
    file(GLOB DIR_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*_test.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*_tests.cpp")
    list(APPEND ALL_TESTS ${DIR_TESTS})
endforeach()
# socket_can_stream_tests.cpp needs a real vcan0 interface + CAP_NET_ADMIN, so it's
# built as its own binary (see socket_can_tests below) and kept out of all_tests.
list(REMOVE_ITEM ALL_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/can_stream/socket_can_stream_tests.cpp")
add_executable(all_tests ${ALL_TESTS})
add_test(all_tests ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/all_tests)

# Tell linker about all libs to be used there
target_link_libraries(all_tests GTest::GTest GTest::Main GTest::gmock j1939_parser can_streams can_sandbox_lib can_reader)
# Make it discoverable
gtest_discover_tests(all_tests)

# Integration tests against a real (virtual) SocketCAN interface. These need
# CAP_NET_ADMIN and a "vcan0" interface, neither available in a plain build sandbox,
# so this is a separate binary that's built here but deliberately not registered via
# add_test/gtest_discover_tests -- see README.md for how to run it.
add_executable(socket_can_tests can_stream/socket_can_stream_tests.cpp)
target_link_libraries(socket_can_tests GTest::GTest GTest::Main can_streams)

# Sets up (best-effort) and runs the vcan0-based integration tests above. The caller
# still needs to grant CAP_NET_ADMIN (e.g. `docker run --cap-add=NET_ADMIN`) for the
# `ip link` step to succeed -- see scripts/vcan_tests.sh and README.md. A real script
# file (rather than inline `sh -c "..."` COMMAND strings) sidesteps CMake/Makefile
# generators re-quoting nested shell operators across layers.
add_custom_target(vcan_tests
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/vcan_tests.sh $<TARGET_FILE:socket_can_tests>
    DEPENDS socket_can_tests
    COMMENT "Setting up vcan0 (best-effort) and running SocketCanStream integration tests"
)

if(ENABLE_COVERAGE)
    find_program(GCOVR_EXECUTABLE gcovr)
    if(GCOVR_EXECUTABLE)
        # Floored slightly below the actual achieved numbers (97.9% line / 93.4%
        # branch as of this writing) so gcovr's own exit code (4) fails the build on a
        # real regression without being brittle against insignificant rounding noise.
        add_custom_target(coverage
            COMMAND $<TARGET_FILE:all_tests>
            COMMAND ${GCOVR_EXECUTABLE} --root ${CMAKE_SOURCE_DIR}
                    --exclude '.*_tests?\.cpp$$' --exclude '.*/build/.*'
                    --exclude-throw-branches
                    --fail-under-line 97.8 --fail-under-branch 93.3
                    --print-summary
                    --html-details ${CMAKE_BINARY_DIR}/coverage.html
                    --txt ${CMAKE_BINARY_DIR}/coverage.txt
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            DEPENDS all_tests
            COMMENT "Running tests and generating coverage report"
        )
    else()
        message(WARNING "ENABLE_COVERAGE is on but gcovr was not found; the 'coverage' target will not be available.")
    endif()
endif()
