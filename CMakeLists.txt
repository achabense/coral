cmake_minimum_required(VERSION 3.25)

project(coral LANGUAGES CXX)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)

FetchContent_Declare(
    sdl3
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
    GIT_TAG        683181b47cfabd293e3ea409f838915b8297a4fd # release-3.4.2
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED FALSE CACHE BOOL "" FORCE)
set(SDL_STATIC TRUE CACHE BOOL "" FORCE) # Necessary for SDL3-static.
FetchContent_MakeAvailable(sdl3)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY "https://github.com/ocornut/imgui.git"
    GIT_TAG        6ded5230d043aa32c755e65c910c2af5002fb9f9 # release-1.92.6
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

file(GLOB SOURCE_LIST CONFIGURE_DEPENDS
    src/*.cpp
    ${imgui_SOURCE_DIR}/*.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCE_LIST})

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:Clang,GNU>:-finput-charset=utf-8 -fexec-charset=utf-8>
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_USER_CONFIG=\"imgui_config.hpp\")
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-static)
