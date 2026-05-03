cmake_minimum_required(VERSION 3.10)
project(tests C)

set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/*.c)
set(TEST_DIR ${CMAKE_SOURCE_DIR}/tests)
file(GLOB_RECURSE TEST_FILES ${TEST_DIR}/*.c)

add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${TEST_FILES}
)
target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else ()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)