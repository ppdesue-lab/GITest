cmake_minimum_required(VERSION 3.10)

set(project_name "kengine")
project(${project_name} VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(KENGINE_RENDERER_API "OpenGL" CACHE STRING "Renderer backend: OpenGL or DX11")
#set_property(CACHE KENGINE_RENDERER_API PROPERTY STRINGS OpenGL DX11)

set(KENGINE_OPENCL_ROOT "D:/lib/opencl" CACHE PATH "OpenCL SDK root used by the path tracer")
set(KENGINE_OPENCL_LIBRARY "${KENGINE_OPENCL_ROOT}/lib/x86_64/OpenCL.lib")
set(KENGINE_OCCT_ROOT "D:/gitclones/CarveraCAM/thirdparty/occt" CACHE PATH "OpenCASCADE installation used by the STEP loader")
set(KENGINE_OCCT_AUX_RUNTIME_DIR "D:/soft/Mayo-0.9.0-win64-binaries" CACHE PATH "Directory containing auxiliary DLLs required by this OCCT build")
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
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)

set(GLAD_API gl=4.6 CACHE STRING "" FORCE)

FetchContent_MakeAvailable(glad)


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

#---------------------------
#find_package(Vulkan REQUIRED)

#------------------third party end
set(third_party_libs
  assimp
  glfw
  glad
  spdlog
  libimgui
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
    D:/gitclones/CarveraCAM/thirdparty/glm
    ${Vulkan_INCLUDE_DIRS}
    ${imgui_SOURCE_DIR}
    ${stb_SOURCE_DIR}
    ${tinybvh_SOURCE_DIR}
    ${KENGINE_OPENCL_ROOT}/include
    ${KENGINE_OPENCL_ROOT}/include/CL
    ${KENGINE_OCCT_ROOT}/inc
)

set(all_compiler_definitions 
  APP_ROOT_PATH=\"${CMAKE_SOURCE_DIR}\"
)


#---------------------------------------------
#add definitions and executable
add_definitions(-DPLATFORM_WINDOWS)
if(KENGINE_RENDERER_API STREQUAL "DX11")
  list(APPEND all_compiler_definitions G_DX11)
  list(APPEND third_party_libs d3d11 dxgi d3dcompiler)
else()
  list(APPEND all_compiler_definitions G_OPENGL)
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

