cmake_minimum_required(VERSION 3.25)
project(holonight-system-services VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
install(FILES LICENSE LICENSES/GPL-3.0-or-later.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")

option(BUILD_TESTS "Build unit and package-consumer tests" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)

add_library(holonight_system_audio STATIC
  include/holonight_system/audio/AudioBackend.h
  include/holonight_system/audio/AudioController.h
  include/holonight_system/audio/AudioDeviceModel.h
  include/holonight_system/audio/AudioStreamModel.h
  include/holonight_system/audio/AudioTypes.h
  src/audio/AudioController.cpp
  src/audio/AudioDeviceModel.cpp
  src/audio/AudioStreamModel.cpp
  src/audio/PulseAudioBackend.cpp
  src/audio/PulseAudioBackend.h
  src/audio/PulseAudioSystem.h
)
add_library(HoloNightSystem::Audio ALIAS holonight_system_audio)
set_target_properties(holonight_system_audio PROPERTIES EXPORT_NAME Audio)
target_include_directories(holonight_system_audio
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/holonight_system/audio>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/holonight_system/audio>
  PRIVATE src/audio
)
target_link_libraries(holonight_system_audio PUBLIC Qt6::Core PRIVATE PkgConfig::LIBPULSE)
target_compile_features(holonight_system_audio PUBLIC cxx_std_23)

install(TARGETS holonight_system_audio EXPORT HoloNightSystemServicesTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/holonight_system/audio
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT HoloNightSystemServicesTargets
  FILE HoloNightSystemServicesTargets.cmake
  NAMESPACE HoloNightSystem::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoloNightSystemServices
)
configure_package_config_file(cmake/HoloNightSystemServicesConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/HoloNightSystemServicesConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoloNightSystemServices
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/HoloNightSystemServicesConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/HoloNightSystemServicesConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/HoloNightSystemServicesConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoloNightSystemServices
)

if(BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  find_package(GTest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
