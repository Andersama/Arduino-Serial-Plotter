# CMakeList.txt : CMake project for ArduinoSerialPlotter, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
#set(VCPKG_BUILD_TYPE release)

find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

#target_link_libraries(main PRIVATE glfw)

# Add source to this project's executable.
add_executable (ArduinoSerialPlotter "ArduinoSerialPlotter.cpp" "ArduinoSerialPlotter.h" "SerialClass.h" "simdjson.h" "simdjson.cpp" "nuklear_glfw_gl4.h" "nuklear.h"   "real_vector.h")
target_link_libraries(ArduinoSerialPlotter PRIVATE GLEW::GLEW glfw fmt::fmt-header-only)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)


# TODO: Add tests and install targets if needed.
