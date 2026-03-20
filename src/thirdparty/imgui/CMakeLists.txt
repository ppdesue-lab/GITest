cmake_minimum_required(VERSION 3.10)

project(libimgui)

#message(STATUS "imgui source dir: ${imgui_SOURCE_DIR}")
#message(STATUS "imgui binary dir: ${imgui_BINARY_DIR}")
set(IMGUI_SRC
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3_loader.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/imgui.h
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        )

add_library(libimgui STATIC ${IMGUI_SRC})
target_include_directories(libimgui PRIVATE ${imgui_SOURCE_DIR})
target_link_libraries(libimgui PRIVATE glfw)