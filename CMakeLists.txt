cmake_minimum_required(VERSION 3.20)
project(MotionBridgeTransformLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(transform2d_lab src/transform2d.cpp)
add_executable(transform3d_lab src/transform3d.cpp)
add_executable(cpp_lifetime_lab src/cpp_lifetime_lab.cpp)
add_executable(longest_consecutive_lab src/longest_consecutive_lab.cpp)
add_executable(quaternion_interpolation_lab src/quaternion_interpolation_lab.cpp)
add_executable(smart_pointer_lab src/smart_pointer_lab.cpp)
add_executable(daily_temperatures_lab src/daily_temperatures_lab.cpp)
add_executable(joint_depth_grouping_lab src/joint_depth_grouping_lab.cpp)
add_executable(bvh_data_structure_lab src/bvh_data_structure_lab.cpp)
add_executable(top_k_frequent_lab src/top_k_frequent_lab.cpp)
add_executable(
    motion_source_polymorphism_lab
    src/motion_source_polymorphism_lab.cpp
)
add_executable(
    redundant_connection_lab
    src/redundant_connection_lab.cpp
)
add_executable(foot_ik_lab src/foot_ik_lab.cpp)
add_executable(realtime_frame_buffer_lab src/realtime_frame_buffer_lab.cpp)
add_executable(lru_cache_lab src/lru_cache_lab.cpp)
add_executable(sliding_window_maximum_lab src/sliding_window_maximum_lab.cpp)

if(MSVC)
    add_compile_options(/EHsc)
    target_compile_options(transform2d_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(transform3d_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(cpp_lifetime_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(longest_consecutive_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(quaternion_interpolation_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(smart_pointer_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(daily_temperatures_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(joint_depth_grouping_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(bvh_data_structure_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(top_k_frequent_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(
        motion_source_polymorphism_lab
        PRIVATE /W4 /permissive- /utf-8
    )
    target_compile_options(
        redundant_connection_lab
        PRIVATE /W4 /permissive- /utf-8
    )
    target_compile_options(foot_ik_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(realtime_frame_buffer_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(lru_cache_lab PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(sliding_window_maximum_lab PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(transform2d_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(transform3d_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(cpp_lifetime_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(longest_consecutive_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(quaternion_interpolation_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(smart_pointer_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(daily_temperatures_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(joint_depth_grouping_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(bvh_data_structure_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(top_k_frequent_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        motion_source_polymorphism_lab
        PRIVATE -Wall -Wextra -Wpedantic
    )
    target_compile_options(
        redundant_connection_lab
        PRIVATE -Wall -Wextra -Wpedantic
    )
    target_compile_options(foot_ik_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(realtime_frame_buffer_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lru_cache_lab PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(sliding_window_maximum_lab PRIVATE -Wall -Wextra -Wpedantic)
endif()
