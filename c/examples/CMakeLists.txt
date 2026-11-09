add_executable(example_frame frame.c)
target_link_libraries(example_frame space_balloon_communication_format)

# 例が動くこと（壊れていないこと）もテストで確かめる
if(BUILD_TESTS)
    add_test(NAME example_frame COMMAND example_frame)
endif()
