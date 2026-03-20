cmake_minimum_required(VERSION 3.10)

set(project_name "kengine")
project(${project_name} VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.h)

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
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)

set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDBS OFF CACHE BOOL "" FORCE)


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

set(GLAD_API gl=3.3 CACHE STRING "" FORCE)

FetchContent_MakeAvailable(glad)


#stb image

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)
FetchContent_MakeAvailable(stb)


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
    GIT_TAG v1.90.6  # 指定版本标签
)

# 获取并构建 imgui
FetchContent_MakeAvailable(imgui)

#make an imgui folder and put all of these files in the imgui folder
add_subdirectory(src/thirdparty/imgui) #libimgui


#------------------third party end
set(third_party_libs
  assimp
  glfw
  glad
  spdlog
  libimgui
)

#definitions and includes
set(all_includes src
    ${CMAKE_SOURCE_DIR}
    D:/gitclones/CarveraCAM/thirdparty/glm
    ${imgui_SOURCE_DIR}
    
)
set(all_compiler_definitions 
  APP_ROOT_PATH=\"${CMAKE_SOURCE_DIR}\"
)


set(all_includes ${all_includes} ${stb_SOURCE_DIR})

#---------------------------------------------
#add definitions and executable
add_definitions(-DG_OPENGL -DPLATFORM_WINDOWS)
add_executable(${project_name} ${SRC_FILES})

#precompiled header
target_precompile_headers(${project_name} PRIVATE src/stdsfx.h src/stdsfx.cpp)

#compiler definitions
target_compile_definitions(${project_name} PUBLIC ${all_compiler_definitions})
#link third party libs
target_link_libraries(${project_name} PRIVATE ${third_party_libs})
#include dirs
target_include_directories(${project_name} PRIVATE ${all_includes})
