add_executable(smoothcam_tests
    unit/CameraFormatTests.cpp
    unit/EffectsTests.cpp
    unit/FlowTests.cpp
    unit/PacerTests.cpp
    unit/SettingsTests.cpp)

target_link_libraries(smoothcam_tests PRIVATE smoothcam_engine GTest::gtest_main d3d11 dxgi)

include(GoogleTest)
gtest_discover_tests(smoothcam_tests DISCOVERY_MODE PRE_TEST)

add_subdirectory(bench)
