cmake_minimum_required(VERSION 2.8.3)
project(jaguar4x4wheel_base)

#Add custom (non compiling) targets so launch scripts and python files show up in QT Creator's project view.
file(GLOB_RECURSE EXTRA_FILES */*)
add_custom_target(${PROJECT_NAME}_OTHER_FILES ALL WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} SOURCES ${EXTRA_FILES})

find_package(catkin REQUIRED COMPONENTS controller_manager hardware_interface
  roslaunch roslint  roscpp sensor_msgs control_toolbox)
find_package(Boost REQUIRED)

catkin_package(
    INCLUDE_DIRS include
    CATKIN_DEPENDS hardware_interface roscpp sensor_msgs control_toolbox
    DEPENDS Boost
)

include_directories(include ${Boost_INCLUDE_DIRS} ${catkin_INCLUDE_DIRS})

add_executable(jaguar4x4wheel_node
  src/jaguar4x4wheel_base.cpp
  src/jaguar4x4wheel_hardware.cpp
  src/DrRobotMotionSensorDriver.cpp)
target_link_libraries(jaguar4x4wheel_node ${catkin_LIBRARIES} ${Boost_LIBRARIES})

roslint_cpp(
  src/jaguar4x4wheel_base.cpp
  src/jaguar4x4wheel_hardware.cpp
  src/DrRobotMotionSensorDriver.cpp
  )

roslaunch_add_file_check(launch)

install(TARGETS jaguar4x4wheel_node
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION})

install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})

install(DIRECTORY launch config
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
