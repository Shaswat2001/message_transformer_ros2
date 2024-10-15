cmake_minimum_required(VERSION 3.8)
project(message_transformer)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)


add_executable(qnx2ros src/qnx2ros.cpp)
ament_target_dependencies(qnx2ros
  rclcpp geometry_msgs nav_msgs sensor_msgs tf2 tf2_ros)

# uncomment the following section in order to fill in
# further dependencies manually.
# find_package(<dependency> REQUIRED)
include_directories(include)
include_directories(/opt/ros/humble/include/tf2_geometry_msgs)
include_directories(/opt/ros/humble/include/tf2)
include_directories(/opt/ros/humble/include/tf2_ros)

install(TARGETS
  qnx2ros
  DESTINATION lib/${PROJECT_NAME})

  install(DIRECTORY include/
  DESTINATION include/
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
