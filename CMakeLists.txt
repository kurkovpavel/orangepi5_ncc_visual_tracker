cmake_minimum_required(VERSION 3.10)
project(VisualTracker)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenCV REQUIRED)
find_package(OpenCL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBEVDEV REQUIRED libevdev)

# Include directories
include_directories(include)
include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${OpenCL_INCLUDE_DIRS})
include_directories(${LIBEVDEV_INCLUDE_DIRS})

# Add executable
add_executable(visual_tracker 
    src/main.cpp 
    src/tracker.cpp 
    src/opencl_utils.cpp
    src/framebuffer/framebuffer.cpp
)

# Link libraries
target_link_libraries(visual_tracker 
    ${OpenCV_LIBS} 
    ${OpenCL_LIBRARIES}
    ${LIBEVDEV_LIBRARIES}
)

# Copy kernel files
#configure_file(kernels/tracker_kernels.cl ${CMAKE_CURRENT_BINARY_DIR}/tracker_kernels.cl COPYONLY)

add_custom_command(
    TARGET visual_tracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/kernels/tracker_kernels.cl
    ${CMAKE_CURRENT_BINARY_DIR}/tracker_kernels.cl
)