include(FetchContent)

# Windows Implementation Libraries (MIT) - header only. We only need the include
# directory, so SOURCE_SUBDIR points somewhere without a CMakeLists.txt to skip
# WIL's own test/packaging targets.
FetchContent_Declare(wil
    GIT_REPOSITORY https://github.com/microsoft/wil.git
    GIT_TAG        v1.0.250325.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-build)
FetchContent_MakeAvailable(wil)

add_library(wil_headers INTERFACE)
target_include_directories(wil_headers SYSTEM INTERFACE ${wil_SOURCE_DIR}/include)

if(SMOOTHCAM_BUILD_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
