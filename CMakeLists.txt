cmake_minimum_required(VERSION 3.20)

project(wal
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(GDK_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)

add_executable(wal
    src/app.cpp
    src/io.cpp
    src/layer_shell.cpp
    src/main.cpp
    src/ui.cpp
)

target_compile_features(wal PRIVATE cxx_std_23)
target_include_directories(wal PRIVATE include)
target_link_libraries(
    wal
    PRIVATE
        Vulkan::Vulkan
        PkgConfig::WAYLAND_CLIENT
        PkgConfig::XKBCOMMON
        PkgConfig::FREETYPE
        PkgConfig::FONTCONFIG
        PkgConfig::GDK_PIXBUF
)
target_compile_definitions(wal PRIVATE SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(SHADER_SOURCES
    src/shaders/ui.vert
    src/shaders/ui.frag
)

set(SPIRV_SHADERS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_SHADER "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_SHADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND ${GLSLANG_VALIDATOR} -V "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -o ${SPIRV_SHADER}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        VERBATIM
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_SHADER})
endforeach()

add_custom_target(wal_shaders DEPENDS ${SPIRV_SHADERS})
add_dependencies(wal wal_shaders)
