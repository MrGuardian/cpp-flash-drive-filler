cmake_minimum_required(VERSION 3.30)
project(usb_fill)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable
add_executable(usb_fill main.cpp)
