add_library(SmoothCamSource SHARED
    Activator.cpp
    MediaSource.cpp
    MediaStream.cpp
    dllmain.cpp
    SmoothCamSource.def
    SmoothCamSource.rc)

target_link_libraries(SmoothCamSource PRIVATE
    smoothcam_engine
    mfplat mf mfuuid mfsensorgroup runtimeobject)

# The person-segmentation model (background blur) lives next to the DLL.
add_custom_command(TARGET SmoothCamSource POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SmoothCamSource>/models
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/assets/models/selfie_segmenter.onnx
            $<TARGET_FILE_DIR:SmoothCamSource>/models/selfie_segmenter.onnx)
