set(SPACE_BALLOON_COMMUNICATION_FORMAT_SOURCES
    src/space_balloon_communication_format.c
)

set(SPACE_BALLOON_COMMUNICATION_FORMAT_HEADERS
    include/space_balloon_communication_format/space_balloon_communication_format.h
    include/space_balloon_communication_format/types.h
    include/space_balloon_communication_format/config.h
)

set(SPACE_BALLOON_COMMUNICATION_FORMAT_PRIVATE_HEADERS
    src/internal.h
)

add_library(space_balloon_communication_format
    ${SPACE_BALLOON_COMMUNICATION_FORMAT_SOURCES}
    ${SPACE_BALLOON_COMMUNICATION_FORMAT_HEADERS}
    ${SPACE_BALLOON_COMMUNICATION_FORMAT_PRIVATE_HEADERS}
)

target_include_directories(space_balloon_communication_format
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(space_balloon_communication_format PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${SPACE_BALLOON_COMMUNICATION_FORMAT_HEADERS}"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)


target_compile_features(space_balloon_communication_format PRIVATE c_std_99)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(space_balloon_communication_format PRIVATE SPACE_BALLOON_COMMUNICATION_FORMAT_BUILDING_DLL)
    target_compile_definitions(space_balloon_communication_format PUBLIC SPACE_BALLOON_COMMUNICATION_FORMAT_DLL)
endif()

install(TARGETS space_balloon_communication_format
    EXPORT space_balloon_communication_format_targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/space_balloon_communication_format
)

install(EXPORT space_balloon_communication_format_targets
    FILE space_balloon_communication_format_targets.cmake
    NAMESPACE space_balloon_communication_format::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/space_balloon_communication_format
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/space_balloon_communication_format_config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/space_balloon_communication_format_config.cmake
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/space_balloon_communication_format_config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/space_balloon_communication_format
)
