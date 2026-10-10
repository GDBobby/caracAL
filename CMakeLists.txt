cmake_minimum_required(VERSION 3.20)
project(caracal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(WARNING "caracAL src is intended to build with GCC (g++)")
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(CARACAL_SOURCES
    src/account_info.cpp
    src/character_thread.cpp
    src/client_scripts.cpp
    src/config_util.cpp
    src/coordinator.cpp
    src/file_stored_key_values.cpp
    src/g_data.cpp
    src/game_files.cpp
    src/game_session.cpp
    src/html_globals.cpp
    src/http_client.cpp
    src/ipc_storage.cpp
    src/log_utils.cpp
    src/monitoring_util.cpp
    src/plugin_host.cpp
    src/realm_rtt.cpp
    src/socket_io.cpp
    src/stream_multiplexer.cpp
    src/websocket.cpp)

add_library(caracal ${CARACAL_SOURCES})
target_include_directories(caracal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(caracal PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(caracal PRIVATE -Wall -Wextra)

add_executable(caracal_headless src/main.cpp)
target_link_libraries(caracal_headless PRIVATE caracal)
target_compile_options(caracal_headless PRIVATE -Wall -Wextra)

add_library(crabs MODULE plugins/crabs.cpp)
target_include_directories(crabs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(crabs PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "crabs"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

add_library(caracal_test_plugin MODULE plugins/test_plugin.cpp)
target_include_directories(caracal_test_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(caracal_test_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "test_plugin"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

add_executable(caracal_src_tests src/tests.cpp)
target_link_libraries(caracal_src_tests PRIVATE caracal)
target_compile_options(caracal_src_tests PRIVATE -Wall -Wextra)
target_compile_definitions(caracal_src_tests PRIVATE
    CARACAL_TEST_PLUGIN="$<TARGET_FILE:caracal_test_plugin>")
add_dependencies(caracal_src_tests caracal_test_plugin)

enable_testing()
add_test(NAME src COMMAND caracal_src_tests)
set_tests_properties(src PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
