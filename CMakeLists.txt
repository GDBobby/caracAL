cmake_minimum_required(VERSION 3.20)
project(caracal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    src/file_stored_key_values.cpp
    src/game_files.cpp
    src/html_globals.cpp
    src/http_client.cpp
    src/ipc_storage.cpp
    src/log_utils.cpp
    src/monitoring_util.cpp
    src/realm_rtt.cpp
    src/stream_multiplexer.cpp)

add_library(caracal ${CARACAL_SOURCES})
target_include_directories(caracal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(caracal PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_options(caracal PRIVATE -Wall -Wextra -Wpedantic)

add_executable(caracal_src_tests src/tests.cpp)
target_link_libraries(caracal_src_tests PRIVATE caracal)
target_compile_options(caracal_src_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME src COMMAND caracal_src_tests)
set_tests_properties(src PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
