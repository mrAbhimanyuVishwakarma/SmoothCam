configure_file(Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)

add_library(smoothcam_core STATIC
    Log.cpp
    Paths.cpp
    Png.cpp
    Settings.cpp
    Status.cpp)

target_include_directories(smoothcam_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(smoothcam_core
    PUBLIC smoothcam_options wil_headers
    PRIVATE shell32 ole32 windowscodecs)
