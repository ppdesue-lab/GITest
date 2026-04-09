cmake_minimum_required(VERSION 3.10)

set(project_name "kengine")
project(${project_name} VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


#make an imgui folder and put all of these files in the imgui folder
add_subdirectory(src) #libimgui

add_executable(sandbox sandbox/sandbox.cpp)
target_link_libraries(sandbox PRIVATE ${kengine_lib})
target_include_directories(sandbox PRIVATE ${kengine_include})
