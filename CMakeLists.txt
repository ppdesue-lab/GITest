cmake_minimum_required(VERSION 3.10)

set(project_name "GITest")
project(${project_name} VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#make an imgui folder and put all of these files in the imgui folder
add_subdirectory(src) #libimgui

add_executable(sandbox sandbox/sandbox.cpp)
target_link_libraries(sandbox PRIVATE ${kengine_lib})
target_include_directories(sandbox PRIVATE ${kengine_include})

set(KENGINE_OCCT_AUX_DLLS
    tbb12.dll
    jemalloc.dll
    openvr_api.dll
    FreeImage.dll
    freetype.dll
    avcodec-57.dll
    avformat-57.dll
    swscale-4.dll
    avutil-55.dll
)
foreach(occt_aux_dll ${KENGINE_OCCT_AUX_DLLS})
    if(NOT EXISTS "${KENGINE_OCCT_AUX_RUNTIME_DIR}/${occt_aux_dll}")
        message(FATAL_ERROR "OCCT STEP import requires ${occt_aux_dll}; set KENGINE_OCCT_AUX_RUNTIME_DIR to its runtime directory.")
    endif()
endforeach()

# Copy dependency DLLs to sandbox output directory at build time
add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/_deps/assimp-build/bin/$<CONFIG>"
        "$<TARGET_FILE_DIR:sandbox>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/_deps/glad-build/$<CONFIG>"
        "$<TARGET_FILE_DIR:sandbox>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/_deps/glfw-build/src/$<CONFIG>"
        "$<TARGET_FILE_DIR:sandbox>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/_deps/spdlog-build/$<CONFIG>"
        "$<TARGET_FILE_DIR:sandbox>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${KENGINE_OCCT_ROOT}/win64/vc14/bin"
        "$<TARGET_FILE_DIR:sandbox>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/tbb12.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/jemalloc.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/openvr_api.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/FreeImage.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/freetype.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/avcodec-57.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/avformat-57.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/swscale-4.dll"
        "${KENGINE_OCCT_AUX_RUNTIME_DIR}/avutil-55.dll"
        "$<TARGET_FILE_DIR:sandbox>"
)
