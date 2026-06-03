cmake_minimum_required(VERSION 3.20)

project(wal
    VERSION 1.0.0
    LANGUAGES CXX
)

include(GNUInstallDirs)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(GDK_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
pkg_check_modules(LIBRSVG REQUIRED IMPORTED_TARGET librsvg-2.0)

add_executable(wal
    src/app.cpp
    src/config.cpp
    src/io.cpp
    src/layer_shell.cpp
    src/main.cpp
    src/ui.cpp
)

target_compile_features(wal PRIVATE cxx_std_23)
target_include_directories(wal PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(
    wal
    PRIVATE
        Vulkan::Vulkan
        PkgConfig::WAYLAND_CLIENT
        PkgConfig::XKBCOMMON
        PkgConfig::FREETYPE
        PkgConfig::FONTCONFIG
        PkgConfig::GDK_PIXBUF
        PkgConfig::LIBRSVG
)

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

set(EMBEDDED_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/wal/embedded_shaders.hpp")
add_custom_command(
    OUTPUT "${EMBEDDED_SHADER_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/wal"
    COMMAND
        ${CMAKE_COMMAND}
        -DVERT_SHADER=${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.vert.spv
        -DFRAG_SHADER=${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.frag.spv
        -DOUTPUT=${EMBEDDED_SHADER_HEADER}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS ${SPIRV_SHADERS} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    VERBATIM
)

add_custom_target(wal_embedded_shaders DEPENDS "${EMBEDDED_SHADER_HEADER}")
add_dependencies(wal wal_embedded_shaders)
target_sources(wal PRIVATE "${EMBEDDED_SHADER_HEADER}")

install(TARGETS wal RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES config.example.toml DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
