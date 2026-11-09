add_executable(example_frame_cpp frame.cpp)
target_link_libraries(example_frame_cpp space_balloon_communication_format_cpp)
set_target_properties(example_frame_cpp PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(example_frame_cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 例が動くこと（壊れていないこと）もテストで確かめる
if(SBCF_CPP_BUILD_TESTS)
    add_test(NAME example_frame_cpp COMMAND example_frame_cpp)
endif()
