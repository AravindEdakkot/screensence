cmake_minimum_required(VERSION 3.20)
project(screensense VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(GNUInstallDirs)

option(SCREENSENSE_BUILD_TESTS "Build unit tests" ON)
# systemd only looks for user units in fixed places. For a per-user install
# into ~/.local, pass -DSCREENSENSE_SYSTEMD_USER_UNIT_DIR=$HOME/.local/share/systemd/user
set(SCREENSENSE_SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user"
    CACHE PATH "Where to install the systemd user unit")

# Only the OpenCV modules we actually use. objdetect provides FaceDetectorYN,
# which runs on the dnn module.
find_package(OpenCV 4.8 REQUIRED COMPONENTS core imgproc videoio objdetect dnn)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

# Pure logic with no camera or desktop dependencies, so it can be unit-tested.
add_library(screensense_core STATIC
  src/config.cpp
  src/tracker.cpp
)
target_include_directories(screensense_core PUBLIC src)
target_compile_options(screensense_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(screensense
  src/main.cpp
  src/camera.cpp
  src/head_pose.cpp
  src/daemon_bus.cpp
  src/engine.cpp
  src/calibrate.cpp
  src/backends/registry.cpp
  src/backends/gnome_backend.cpp
  src/backends/null_backend.cpp
)
target_link_libraries(screensense PRIVATE screensense_core ${OpenCV_LIBS} PkgConfig::GIO)
target_compile_options(screensense PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(screensense PRIVATE
  SCREENSENSE_VERSION="${PROJECT_VERSION}"
  SCREENSENSE_INSTALLED_MODEL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/screensense/models"
  SCREENSENSE_SOURCE_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)

install(TARGETS screensense RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES models/face_detection_yunet_2023mar.onnx
        DESTINATION ${CMAKE_INSTALL_DATADIR}/screensense/models)
configure_file(data/systemd/screensense.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/screensense.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/screensense.service
        DESTINATION ${SCREENSENSE_SYSTEMD_USER_UNIT_DIR})
install(DIRECTORY gnome-extension/screensense@aravindedakkot.gmail.com
        DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions)

if(SCREENSENSE_BUILD_TESTS)
  enable_testing()
  add_executable(test_tracker tests/test_tracker.cpp)
  target_link_libraries(test_tracker PRIVATE screensense_core)
  add_test(NAME tracker COMMAND test_tracker)
  add_executable(test_config tests/test_config.cpp)
  target_link_libraries(test_config PRIVATE screensense_core)
  add_test(NAME config COMMAND test_config)
endif()
