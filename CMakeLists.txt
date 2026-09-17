cmake_minimum_required(VERSION 3.28)
project(MHWI-VisualControllerExtended VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This plugin requires an x64 build")
endif()

# Fixed official amalgamations; no network access is needed during builds.
add_library(vce_zydis STATIC third_party/safetyhook/Zydis.c)
target_include_directories(vce_zydis PUBLIC third_party/safetyhook)
target_compile_definitions(vce_zydis PUBLIC ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
add_library(vce_safetyhook STATIC third_party/safetyhook/safetyhook.cpp)
target_include_directories(vce_safetyhook PUBLIC third_party/safetyhook)
target_link_libraries(vce_safetyhook PUBLIC vce_zydis)

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
    vce_safetyhook psapi user32
)

set_target_properties(MHWI-VisualControllerExtended PROPERTIES
    PREFIX ""
    OUTPUT_NAME "MHWI-VisualControllerExtended"
)
