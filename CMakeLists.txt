cmake_minimum_required(VERSION 3.20)
project(MotionBridgeTransformLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(transform3d_lab src/transform3d.cpp)
add_executable(quaternion_interpolation_lab src/quaternion_interpolation_lab.cpp)
add_executable(joint_depth_grouping_lab src/joint_depth_grouping_lab.cpp)
add_executable(bvh_data_structure_lab src/bvh_data_structure_lab.cpp)
add_executable(
    motion_source_polymorphism_lab
    src/motion_source_polymorphism_lab.cpp
)
add_executable(foot_ik_lab src/foot_ik_lab.cpp)
add_executable(realtime_frame_buffer_lab src/realtime_frame_buffer_lab.cpp)

set(MOTIONBRIDGE_LABS
    transform3d_lab
    quaternion_interpolation_lab
    joint_depth_grouping_lab
    bvh_data_structure_lab
    motion_source_polymorphism_lab
    foot_ik_lab
    realtime_frame_buffer_lab
)

if(MSVC)
    add_compile_options(/EHsc)
    foreach(TARGET_NAME IN LISTS MOTIONBRIDGE_LABS)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
else()
    foreach(TARGET_NAME IN LISTS MOTIONBRIDGE_LABS)
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
