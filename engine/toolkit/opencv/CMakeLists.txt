# The opencv package: image processing blocks with images on the wires. Optional (needs OpenCV), so it
# is its own library: paperboat::toolkit_opencv. See include/paperboat/toolkit/opencv.hpp.

add_library(paperboat_toolkit_opencv src/image.cpp src/blocks.cpp)
add_library(paperboat::toolkit_opencv ALIAS paperboat_toolkit_opencv)
target_include_directories(paperboat_toolkit_opencv PUBLIC include)
target_link_libraries(paperboat_toolkit_opencv
  PUBLIC paperboat::toolkit opencv_core opencv_imgproc
  PRIVATE opencv_imgcodecs paperboat_warnings)
