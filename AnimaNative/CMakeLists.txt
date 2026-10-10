cmake_minimum_required(VERSION 3.24)
project(AnimaNative LANGUAGES CXX)

if(NOT WIN32)
    message(STATUS "AnimaNative is a Windows-only Direct3D 12 project. Open AnimaNative.sln on Windows with Visual Studio 2022.")
    return()
endif()

add_executable(AnimaNative WIN32
    src/main.cpp
    src/NativeAnima.cpp
    src/NativeRenderer.cpp
    src/NativeAnima.h
    src/NativeRenderer.h
)
target_compile_features(AnimaNative PRIVATE cxx_std_20)
target_compile_definitions(AnimaNative PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(AnimaNative PRIVATE d3d12 dxgi d3dcompiler dxguid user32 gdi32 shell32)

add_custom_command(TARGET AnimaNative POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnimaNative>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/shaders/room_compute.hlsl" "$<TARGET_FILE_DIR:AnimaNative>/Shaders/room_compute.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/shaders/blit.hlsl" "$<TARGET_FILE_DIR:AnimaNative>/Shaders/blit.hlsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnimaNative>/Examples"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Examples/raytrace-room.anima" "$<TARGET_FILE_DIR:AnimaNative>/Examples/raytrace-room.anima"
)
