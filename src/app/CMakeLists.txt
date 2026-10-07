add_executable(SmoothCam WIN32
    CameraReader.cpp
    Diagnostics.cpp
    MainWindow.cpp
    PreviewRenderer.cpp
    Registration.cpp
    main.cpp
    SmoothCam.rc
    SmoothCam.manifest)

target_link_libraries(SmoothCam PRIVATE
    smoothcam_engine
    comctl32 uxtheme dwmapi shell32 advapi32 dxgi d2d1 dwrite
    mfplat mf mfreadwrite mfuuid mfsensorgroup)

# The RC compiler resolves the icon/notices paths relative to this folder;
# make sure edits to them trigger a rebuild.
set_property(SOURCE SmoothCam.rc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_SOURCE_DIR}/assets/icons/SmoothCam.ico
    ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    ${CMAKE_SOURCE_DIR}/LICENSE)
