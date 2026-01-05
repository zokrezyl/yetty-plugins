# Plugin shared libraries - discovered at runtime via dlopen

# Common function to create a plugin shared library
function(add_yetty_plugin NAME)
    cmake_parse_arguments(PLUGIN "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})

    add_library(${NAME}_plugin SHARED ${PLUGIN_SOURCES})

    target_include_directories(${NAME}_plugin PRIVATE
        ${yetty_SOURCE_DIR}/include
        ${yetty_SOURCE_DIR}/src
        ${PLUGIN_INCLUDES}
    )

    # Link against yetty_core (provides Font, FontManager, RichText, WebGPUContext)
    target_link_libraries(${NAME}_plugin PRIVATE
        yetty_core
        yaml-cpp
        ${PLUGIN_LIBS}
    )

    # Output to plugins directory
    set_target_properties(${NAME}_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        OUTPUT_NAME "${NAME}"
        PREFIX ""  # No 'lib' prefix
    )

    # Ensure position independent code
    set_target_properties(${NAME}_plugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# pdf plugin - uses RichText for rendering
add_yetty_plugin(pdf
    SOURCES pdf/pdf.cpp
    LIBS mupdf
)

# video plugin (Unix only - FFmpeg build requires ./configure + make)
if(UNIX)
    add_yetty_plugin(video
        SOURCES video/video.cpp
        LIBS ffmpeg
    )
endif()

# python plugin (Unix only - Python build requires ./configure + make)
if(UNIX)
    add_yetty_plugin(python
        SOURCES
            python/python.cpp
            python/yetty_wgpu.cpp
        LIBS python_embedded
    )

    # Add compile definition for Python module path
    target_compile_definitions(python_plugin PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    )

    # Copy Python helper modules to build directory
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/python)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/python/yetty_pygfx.py
        ${CMAKE_BINARY_DIR}/python/yetty_pygfx.py
        COPYONLY
    )
endif()
