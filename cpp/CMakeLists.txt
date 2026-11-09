cmake_minimum_required(VERSION 3.16)

project(space_balloon_communication_format_cpp
    VERSION 1.0.0
    DESCRIPTION "C++ wrapper of the space balloon communication format C library"
    LANGUAGES C CXX
)

option(SBCF_CPP_BUILD_TESTS "Build tests of the C++ wrapper" ON)
option(SBCF_CPP_BUILD_EXAMPLES "Build examples of the C++ wrapper" ON)

# 処理の中身は C 版にあるので，それをそのまま取り込む（C 版のテストと例はここではビルドしない）
set(BUILD_TESTS OFF)
set(BUILD_EXAMPLES OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../c ${CMAKE_CURRENT_BINARY_DIR}/c)

# ヘッダだけのライブラリ．リンクすると C 版も一緒にリンクされる
add_library(space_balloon_communication_format_cpp INTERFACE)
target_include_directories(space_balloon_communication_format_cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(space_balloon_communication_format_cpp INTERFACE space_balloon_communication_format)
target_compile_features(space_balloon_communication_format_cpp INTERFACE cxx_std_11)

if(SBCF_CPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SBCF_CPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
