cmake_minimum_required(VERSION 3.21)
project(MAVLinkSimulator VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_GUI "Build the Qt desktop application" ON)
include(CTest)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink/ardupilotmega/mavlink.h")
    message(FATAL_ERROR "Initialize MAVLink: git submodule update --init --recursive")
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    file(GLOB MAVLINK_XML_DEFS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink/message_definitions/*.xml")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/src/mavlink_enums.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/mavlink_enums.h"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_mavlink_enums.py"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_mavlink_enums.py" ${MAVLINK_XML_DEFS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating MAVLink enum lookup tables from XML definitions"
        VERBATIM
    )
    add_custom_target(update-enums
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_mavlink_enums.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Regenerating MAVLink enum tables from XML definitions"
        VERBATIM
    )
endif()
add_library(mavcore src/core.cpp src/mavlink_enums.cpp)
target_include_directories(mavcore PUBLIC src)
target_compile_definitions(mavcore PUBLIC MAVLINK_USE_MESSAGE_INFO)
target_include_directories(mavcore SYSTEM PUBLIC third_party/mavlink)
target_compile_options(mavcore PRIVATE -Wall -Wextra -Wpedantic)
if(BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets SerialPort)
    set(CMAKE_AUTOMOC ON)
    execute_process(
        COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink" rev-parse --short HEAD
        OUTPUT_VARIABLE MAVLINK_LIB_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT MAVLINK_LIB_GIT_HASH)
        set(MAVLINK_LIB_GIT_HASH "unknown")
    endif()
    add_library(mavdesktop src/worker.cpp src/window.cpp)
    target_compile_definitions(mavdesktop PRIVATE
        MAVLINK_SIMULATOR_VERSION="${PROJECT_VERSION}"
        MAVLINK_LIB_GIT_HASH="${MAVLINK_LIB_GIT_HASH}"
    )
    target_link_libraries(mavdesktop PUBLIC mavcore Qt6::Widgets Qt6::SerialPort)
    target_compile_options(mavdesktop PRIVATE -Wall -Wextra -Wpedantic)
    add_executable(mavlink-simulator src/main.cpp)
    target_link_libraries(mavlink-simulator PRIVATE mavdesktop)
    install(TARGETS mavlink-simulator RUNTIME DESTINATION bin)
endif()
if(BUILD_TESTING)
    add_executable(core-tests tests/core_tests.cpp)
    target_link_libraries(core-tests PRIVATE mavcore)
    add_test(NAME core COMMAND core-tests)
    if(BUILD_GUI)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_executable(desktop-tests tests/desktop_tests.cpp)
        target_link_libraries(desktop-tests PRIVATE mavdesktop Qt6::Test util)
        add_test(NAME desktop COMMAND desktop-tests)
        set_tests_properties(desktop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(control-lines-tests tests/control_lines_tests.cpp)
        target_link_libraries(control-lines-tests PRIVATE mavdesktop Qt6::Test util)
        target_link_options(control-lines-tests PRIVATE -Wl,--wrap=ioctl -Wl,--wrap=ttyname_r)
        add_test(NAME control-lines COMMAND control-lines-tests)
    endif()
endif()
