# Developer tool: runs the engine in-process against a real camera, without the
# virtual camera or admin rights. Not shipped.
add_executable(smoothcam_bench main.cpp)
target_link_libraries(smoothcam_bench PRIVATE smoothcam_engine ole32 mfplat windowsapp)
add_dependencies(smoothcam_bench SmoothCamSource) # copies the segmentation model into bin/models
