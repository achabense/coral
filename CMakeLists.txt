cmake_minimum_required(VERSION 3.25)

project(exp LANGUAGES CXX)

file(GLOB SOURCE_LIST CONFIGURE_DEPENDS src/main.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_LIST})

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:Clang,GNU>:-finput-charset=utf-8 -fexec-charset=utf-8>
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE src)
