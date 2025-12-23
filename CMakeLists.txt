cmake_minimum_required(VERSION 3.25)

project(exp LANGUAGES CXX)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
    GIT_TAG        7f3ae3d57459e59943a4ecfefc8f6277ec6bf540 # release-3.2.28
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED FALSE CACHE BOOL "" FORCE)
set(SDL_STATIC TRUE CACHE BOOL "" FORCE) # Necessary for SDL3-static.
FetchContent_MakeAvailable(SDL3)

file(GLOB SOURCE_LIST CONFIGURE_DEPENDS src/*.cpp imgui/*.cpp imgui/backends/*.cpp)

add_executable(${PROJECT_NAME} WIN32 ${SOURCE_LIST})

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:Clang,GNU>:-finput-charset=utf-8 -fexec-charset=utf-8>
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE src imgui imgui/backends)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-static)
