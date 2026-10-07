# Compiles HLSL compute shaders with fxc into headers containing byte arrays,
# so the shaders ship inside the binaries instead of as loose files.
#
#   smoothcam_compile_shaders(TARGET <target> SOURCES a.hlsl b.hlsl INCLUDE_DIRS dir1 dir2)
#
# Every shader must have a `main` entry point. For Foo.hlsl the generated header
# is <build>/shaders/Foo.h and the array is named g_Foo.

find_program(FXC_EXECUTABLE fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkBinPath}/x64"
    REQUIRED)

function(smoothcam_compile_shaders)
    cmake_parse_arguments(ARG "" "TARGET" "SOURCES;DEPENDS;INCLUDE_DIRS" ${ARGN})

    set(include_flags)
    foreach(dir ${ARG_INCLUDE_DIRS})
        list(APPEND include_flags /I ${dir})
    endforeach()

    set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${out_dir})

    set(headers)
    foreach(src ${ARG_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        get_filename_component(abs ${src} ABSOLUTE)
        set(header ${out_dir}/${name}.h)
        add_custom_command(
            OUTPUT ${header}
            COMMAND ${FXC_EXECUTABLE} /nologo /T cs_5_0 /E main /O3 /Qstrip_reflect
                    ${include_flags} /Vn g_${name} /Fh ${header} ${abs}
            DEPENDS ${abs} ${ARG_DEPENDS}
            COMMENT "fxc ${name}.hlsl"
            VERBATIM)
        list(APPEND headers ${header})
    endforeach()

    add_custom_target(${ARG_TARGET}_shaders DEPENDS ${headers})
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_shaders)
    target_include_directories(${ARG_TARGET} PRIVATE ${out_dir})
endfunction()
