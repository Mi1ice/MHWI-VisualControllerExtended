cmake_minimum_required(VERSION 3.20)
project(MHWI-VisualControllerExtended VERSION 1.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)

add_library(MHWI-VisualControllerExtended SHARED
    MHWI-VisualControllerExtended.cpp
)

target_compile_definitions(MHWI-VisualControllerExtended PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WINDOWS _USRDLL
)

if(MSVC)
    target_compile_options(MHWI-VisualControllerExtended PRIVATE /W4 /utf-8)
endif()

target_link_libraries(MHWI-VisualControllerExtended PRIVATE
    minhook psapi user32
)

set_target_properties(MHWI-VisualControllerExtended PROPERTIES
    PREFIX ""
    OUTPUT_NAME "MHWI-VisualControllerExtended"
)
