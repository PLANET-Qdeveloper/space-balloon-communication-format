foreach(name test_space_balloon_communication_format test_tlv)
    add_executable(${name}_cpp ${name}.cpp)
    target_link_libraries(${name}_cpp space_balloon_communication_format_cpp)
    # Arduino の古い環境でも使えるように C++11 で確かめる
    set_target_properties(${name}_cpp PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name}_cpp PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name}_cpp COMMAND ${name}_cpp)
endforeach()
