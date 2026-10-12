cmake_minimum_required(VERSION 3.20)
project(caracal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
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
    src/pathing.cpp
    src/plugin_host.cpp
    src/realm_rtt.cpp
    src/socket_io.cpp
    src/stream_multiplexer.cpp
    src/websocket.cpp
    src/Logger.cpp    
)



add_library(${PROJECT_NAME} ${CARACAL_SOURCES})


target_precompile_headers(${PROJECT_NAME} PUBLIC 
    <vector>
    <meta>
    <array>
    <cstdint>
    <filesystem>
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})

target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall 
        -Wextra 
        -Wshadow -Werror=shadow
        -freflection
        -Wreorder -Werror=reorder
        -Wreturn-type -Werror=return-type
        -Wno-missing-field-initializers
        -Werror=uninitialized
        -Werror=delete-non-virtual-dtor
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 
        /we4456 /we4457 /we4458 /we4459
    >
)

add_executable(${PROJECT_NAME}_headless src/main.cpp)
target_link_libraries(${PROJECT_NAME}_headless PRIVATE ${PROJECT_NAME})
target_compile_options(${PROJECT_NAME}_headless PUBLIC
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall 
        -Wextra 
        -Wshadow -Werror=shadow
        -freflection
        -Wreorder -Werror=reorder
        -Wreturn-type -Werror=return-type
        -Wno-missing-field-initializers
        -Werror=uninitialized
        -Werror=delete-non-virtual-dtor
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 
        /we4456 /we4457 /we4458 /we4459
    >
)

add_library(crabs MODULE plugins/crabs.cpp src/Logger.cpp)
target_include_directories(crabs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(crabs PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-freflection>)
set_target_properties(crabs PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "crabs"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

add_library(${PROJECT_NAME}_test_plugin MODULE plugins/test_plugin.cpp)
target_include_directories(${PROJECT_NAME}_test_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(${PROJECT_NAME}_test_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "test_plugin"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

add_executable(${PROJECT_NAME}_src_tests src/tests.cpp)
target_link_libraries(${PROJECT_NAME}_src_tests PRIVATE ${PROJECT_NAME})
target_compile_options(${PROJECT_NAME}_src_tests PRIVATE -Wall -Wextra)
target_compile_definitions(${PROJECT_NAME}_src_tests PRIVATE
    CARACAL_TEST_PLUGIN="$<TARGET_FILE:${PROJECT_NAME}_test_plugin>")
add_dependencies(${PROJECT_NAME}_src_tests ${PROJECT_NAME}_test_plugin)

enable_testing()
add_test(NAME src COMMAND ${PROJECT_NAME}_src_tests)
set_tests_properties(src PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
