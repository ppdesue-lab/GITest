cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

set(project_name "kengine")
project(${project_name} VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(KENGINE_RENDERER_API "OpenGL" CACHE STRING "Renderer backend: OpenGL or DX11")
#set_property(CACHE KENGINE_RENDERER_API PROPERTY STRINGS OpenGL DX11)
option(KENGINE_SHOW_PROFILE "Enable render flow profiling logs" OFF)

set(KENGINE_OPENCL_ROOT "D:/lib/opencl" CACHE PATH "OpenCL SDK root used by the path tracer")
set(KENGINE_OPENCL_LIBRARY "${KENGINE_OPENCL_ROOT}/lib/x86_64/OpenCL.lib")
set(KENGINE_OCCT_ROOT "D:/gitclones/CarveraCAM/thirdparty/occt" CACHE PATH "OpenCASCADE installation used by the STEP loader")
set(KENGINE_OCCT_AUX_RUNTIME_DIR "D:/soft/Mayo-0.9.0-win64-binaries" CACHE PATH "Directory containing auxiliary DLLs required by this OCCT build")
set(KENGINE_GLM_ROOT "D:/gitclones/CarveraCAM/thirdparty/glm" CACHE PATH "Project GLM include root")
set(KENGINE_TOOLPATH4D_ROOT "D:/gitclones/MakeraStudio/trunk/src/thirdparty/toolpath4d" CACHE PATH "4D toolpath core source directory")
set(KENGINE_OCCT_LIB_DIR "${KENGINE_OCCT_ROOT}/win64/vc14/lib")
set(KENGINE_OCCT_LIBRARIES
  "${KENGINE_OCCT_LIB_DIR}/TKernel.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKMath.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKG2d.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKG3d.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKGeomBase.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKBRep.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKGeomAlgo.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKTopAlgo.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKShHealing.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKMesh.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKDE.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKXSBase.lib"
  "${KENGINE_OCCT_LIB_DIR}/TKDESTEP.lib"
)

file(GLOB_RECURSE SRC_FILES *.cpp *.h)
list(FILTER SRC_FILES EXCLUDE REGEX ".*/thirdparty/ImNodes/.*")
list(FILTER SRC_FILES EXCLUDE REGEX ".*/thirdparty/tanim/.*")
list(FILTER SRC_FILES EXCLUDE REGEX ".*/thirdparty/entt/.*")
list(FILTER SRC_FILES EXCLUDE REGEX ".*/thirdparty/imgui_compat/.*")
list(FILTER SRC_FILES EXCLUDE REGEX ".*/thirdparty/dime/.*")
list(FILTER SRC_FILES EXCLUDE REGEX ".*/GeometryProcess/.*")
if(KENGINE_RENDERER_API STREQUAL "DX11")
  list(FILTER SRC_FILES EXCLUDE REGEX ".*/Platform/OpenGL/.*")
else()
  list(FILTER SRC_FILES EXCLUDE REGEX ".*/Platform/DX11/.*")
endif()
list(APPEND SRC_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/Import/GeometryProcess/SlicePreviewObject.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Import/GeometryProcess/SlicePreviewObject.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Import/Image/TexturePlaneObject.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Import/Image/TexturePlaneObject.h
)

#------------------third party
include(FetchContent)
#assimp
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG v5.3.1
)


set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
#set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
#set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
#
#set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
#set(ASSIMP_BUILD_STL_IMPORTER ON CACHE BOOL "" FORCE)
#set(ASSIMP_BUILD_PLY_IMPORTER ON CACHE BOOL "" FORCE)
#set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
#set(ASSIMP_BUILD_MMD_IMPORTER ON CACHE BOOL "" FORCE)
#set(ASSIMP_INSTALL_PDBS OFF CACHE BOOL "" FORCE)


FetchContent_MakeAvailable(assimp)

add_library(GeometryProcess STATIC
  ${CMAKE_SOURCE_DIR}/src/GeometryProcess/glm-aabb/aabb.cpp
)
target_include_directories(GeometryProcess PUBLIC
  ${CMAKE_SOURCE_DIR}/src/GeometryProcess
  ${KENGINE_GLM_ROOT}
)
target_link_libraries(GeometryProcess PUBLIC assimp)

if(EXISTS "${KENGINE_TOOLPATH4D_ROOT}/CMakeLists.txt")
  add_subdirectory("${KENGINE_TOOLPATH4D_ROOT}" "${CMAKE_BINARY_DIR}/toolpath4d-build" EXCLUDE_FROM_ALL)
  target_include_directories(path4d_toolpath_core PUBLIC ${KENGINE_GLM_ROOT})
else()
  message(FATAL_ERROR "KENGINE_TOOLPATH4D_ROOT does not contain CMakeLists.txt: ${KENGINE_TOOLPATH4D_ROOT}")
endif()

#glfw
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.8
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

#glad
if(NOT KENGINE_RENDERER_API STREQUAL "DX11")
  FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.36
  )

  set(GLAD_API gl=4.6 CACHE STRING "" FORCE)

  FetchContent_MakeAvailable(glad)
endif()


#stb image

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# tinybvh provides the GPU BVH layout and the lightweight OpenCL wrapper used by PathTracer.
FetchContent_Declare(
  tinybvh
  GIT_REPOSITORY https://github.com/jbikker/tinybvh.git
  GIT_TAG dev
)
FetchContent_GetProperties(tinybvh)
if(NOT tinybvh_POPULATED)
  FetchContent_Populate(tinybvh)
endif()

# dime parses DXF files for the shared 2D vector import path.
if(EXISTS "${CMAKE_SOURCE_DIR}/src/thirdparty/dime/include/dime/Model.h")
  set(dime_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/thirdparty/dime")
else()
  FetchContent_Declare(
    dime
    GIT_REPOSITORY https://github.com/coin3d/dime.git
    GIT_TAG master
  )
  FetchContent_GetProperties(dime)
  if(NOT dime_POPULATED)
    FetchContent_Populate(dime)
  endif()
endif()
file(GLOB_RECURSE DIME_SRC "${dime_SOURCE_DIR}/src/*.cpp")
list(FILTER DIME_SRC EXCLUDE REGEX ".*/convert/.*")
add_library(dime STATIC ${DIME_SRC})
target_include_directories(dime PUBLIC "${dime_SOURCE_DIR}/include")
target_compile_definitions(dime PRIVATE DIME_INTERNAL)
target_compile_definitions(dime INTERFACE DIME_NOT_DLL)

# entt for timeline animation editing/playback
if(EXISTS "${CMAKE_SOURCE_DIR}/src/thirdparty/entt/CMakeLists.txt")
  add_subdirectory(thirdparty/entt)
  set(entt_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/thirdparty/entt")
else()
  FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.15.0
  )
  FetchContent_MakeAvailable(entt)
endif()


#fetch spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.11.0
)
FetchContent_MakeAvailable(spdlog)

#fetch imgui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)

# 获取并构建 imgui
FetchContent_MakeAvailable(imgui)


#make an imgui folder and put all of these files in the imgui folder
add_subdirectory(thirdparty/imgui) #libimgui

set(TANIM_ROOT ${CMAKE_SOURCE_DIR}/src/thirdparty/tanim)
add_library(tanim STATIC
  ${TANIM_ROOT}/src/bezier.cpp
  ${TANIM_ROOT}/src/curve_functions.cpp
  ${TANIM_ROOT}/src/sequencer.cpp
  ${TANIM_ROOT}/src/tanim.cpp
  ${TANIM_ROOT}/src/timeliner.cpp
)
target_include_directories(tanim PUBLIC
  ${TANIM_ROOT}/include
  ${KENGINE_GLM_ROOT}
  ${TANIM_ROOT}/external/visit_struct/include
  ${TANIM_ROOT}/external/magic_enum/include
  ${TANIM_ROOT}/external/nlohmann/include
  ${CMAKE_SOURCE_DIR}/src/thirdparty/imgui_compat
)
target_link_libraries(tanim PUBLIC libimgui EnTT::EnTT)

#---------------------------
#find_package(Vulkan REQUIRED)

#------------------third party end
set(third_party_libs
  assimp
  glfw
  spdlog
  libimgui
  tanim
  dime
  path4d_toolpath_core
  GeometryProcess
  EnTT::EnTT
  ${KENGINE_OPENCL_LIBRARY}
  ${KENGINE_OCCT_LIBRARIES}
#  Vulkan::Vulkan
#C:/VulkanSDK/1.3.275.0/Lib/shaderc_shared.lib
#C:/VulkanSDK/1.3.275.0/Lib/spirv-cross-core.lib
#C:/VulkanSDK/1.3.275.0/Lib/spirv-cross-glsl.lib
)


#definitions and includes
set(all_includes
    ${CMAKE_SOURCE_DIR}/src
    ${KENGINE_GLM_ROOT}
    ${Vulkan_INCLUDE_DIRS}
    ${imgui_SOURCE_DIR}
    ${stb_SOURCE_DIR}
    ${tinybvh_SOURCE_DIR}
    ${entt_SOURCE_DIR}/src
    ${TANIM_ROOT}/include
    ${TANIM_ROOT}/external/visit_struct/include
    ${TANIM_ROOT}/external/magic_enum/include
    ${TANIM_ROOT}/external/nlohmann/include
    ${CMAKE_SOURCE_DIR}/src/thirdparty/imgui_compat
    ${KENGINE_OPENCL_ROOT}/include
    ${KENGINE_OPENCL_ROOT}/include/CL
    ${KENGINE_OCCT_ROOT}/inc
)

set(all_compiler_definitions 
  APP_ROOT_PATH=\"${CMAKE_SOURCE_DIR}\"
)

if(KENGINE_SHOW_PROFILE)
  list(APPEND all_compiler_definitions SHOW_PROFILE)
endif()


#---------------------------------------------
#add definitions and executable
add_definitions(-DPLATFORM_WINDOWS)
if(KENGINE_RENDERER_API STREQUAL "DX11")
  list(APPEND all_compiler_definitions G_DX11)
  list(APPEND third_party_libs d3d11 dxgi d3dcompiler)
else()
  list(APPEND all_compiler_definitions G_OPENGL)
  list(APPEND third_party_libs glad)
endif()
#add_executable(${project_name} ${SRC_FILES})

add_library(${project_name} STATIC ${SRC_FILES})

#precompiled header
target_precompile_headers(${project_name} PRIVATE stdsfx.h stdsfx.cpp)

#compiler definitions
target_compile_definitions(${project_name} PUBLIC ${all_compiler_definitions})
#link third party libs
target_link_libraries(${project_name} PUBLIC ${third_party_libs})
if(MSVC)
  # tiny_ocl.h emits a sample-project-relative default library; use the configured SDK library above.
  target_link_options(${project_name} INTERFACE "/NODEFAULTLIB:../external/OpenCL/lib/OpenCL.lib")
endif()
#include dirs
target_include_directories(${project_name} PRIVATE ${all_includes})


#export defines to demo application
set(kengine_include ${all_includes} PARENT_SCOPE)
set(kengine_lib kengine ${third_party_libs} PARENT_SCOPE)

