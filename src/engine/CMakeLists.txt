add_library(smoothcam_engine STATIC
    Engine.cpp
    capture/Camera.cpp
    compositor/Compositor.cpp
    compositor/FsrConstants.cpp
    compositor/NisConstants.cpp
    flow/ComputeFlow.cpp
    gpu/Gpu.cpp
    pacing/Pacer.cpp
    vision/AutoFramer.cpp
    vision/Vision.cpp)

target_include_directories(smoothcam_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# AMD FidelityFX FSR 1 headers (MIT), used by the upscaler.
set(fsr_dir ${CMAKE_SOURCE_DIR}/third_party/fsr)
set_source_files_properties(compositor/FsrConstants.cpp PROPERTIES INCLUDE_DIRECTORIES ${fsr_dir})
# NVIDIA Image Scaling headers (MIT), the alternative upscaler.
set(nis_dir ${CMAKE_SOURCE_DIR}/third_party/nis)
set_source_files_properties(compositor/NisConstants.cpp PROPERTIES INCLUDE_DIRECTORIES ${nis_dir})

target_link_libraries(smoothcam_engine
    PUBLIC smoothcam_core
    PRIVATE d3d11 dxgi mfplat mfreadwrite mf mfuuid strmiids windowsapp)

if(SMOOTHCAM_WITH_NVOF)
    target_sources(smoothcam_engine PRIVATE flow/NvofFlow.cpp)
    target_compile_definitions(smoothcam_engine PUBLIC SMOOTHCAM_WITH_NVOF=1)
    # NVIDIA's BSD-licensed interface headers, plus our minimal <cuda.h>.
    target_include_directories(smoothcam_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/nvof
        ${CMAKE_CURRENT_SOURCE_DIR}/flow/cudalite)
else()
    target_sources(smoothcam_engine PRIVATE flow/NvofStub.cpp)
endif()

set(shader_dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
smoothcam_compile_shaders(
    TARGET smoothcam_engine
    INCLUDE_DIRS ${shader_dir} ${fsr_dir} ${nis_dir}
    DEPENDS ${shader_dir}/Common.hlsli ${fsr_dir}/ffx_a.h ${fsr_dir}/ffx_fsr1.h ${nis_dir}/NIS_Scaler.h
    SOURCES
        ${shader_dir}/BgComposite.hlsl
        ${shader_dir}/BgDown.hlsl
        ${shader_dir}/BlockMatch.hlsl
        ${shader_dir}/Blur.hlsl
        ${shader_dir}/Composite.hlsl
        ${shader_dir}/Consistency.hlsl
        ${shader_dir}/Denoise.hlsl
        ${shader_dir}/Downsample.hlsl
        ${shader_dir}/FsrEasu.hlsl
        ${shader_dir}/FsrRcas.hlsl
        ${shader_dir}/LowLight.hlsl
        ${shader_dir}/LumaStats.hlsl
        ${shader_dir}/NisScaler.hlsl
        ${shader_dir}/NvofConvert.hlsl
        ${shader_dir}/ScaleUV.hlsl
        ${shader_dir}/SmallLuma.hlsl
        ${shader_dir}/SmallRgb.hlsl
        ${shader_dir}/ToBgra.hlsl
        ${shader_dir}/VectorMedian.hlsl)
