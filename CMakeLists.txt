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

option(BUILD_AUDIO "Build the Audio component" ON)
option(BUILD_STORAGE "Build the Storage component" ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
if(BUILD_AUDIO)
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

  install(TARGETS holonight_system_audio EXPORT HoloNightSystemAudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/holonight_system/audio
  )
endif()

if(BUILD_STORAGE)
  find_package(Qt6 REQUIRED COMPONENTS DBus)
  add_library(holonight_system_storage STATIC
    include/holonight_system/storage/StorageTypes.h
    include/holonight_system/storage/StorageBackend.h
    include/holonight_system/storage/StorageController.h
    include/holonight_system/storage/StorageDriveModel.h
    include/holonight_system/storage/StorageVolumeModel.h
    src/storage/StorageController.cpp
    src/storage/StorageDriveModel.cpp
    src/storage/StorageVolumeModel.cpp
    src/storage/UDisks2Backend.cpp
    src/storage/UDisks2Backend.h
  )
  add_library(HoloNightSystem::Storage ALIAS holonight_system_storage)
  set_target_properties(holonight_system_storage PROPERTIES EXPORT_NAME Storage)
  target_include_directories(holonight_system_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/holonight_system/storage>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/holonight_system/storage>
    PRIVATE src/storage)
  target_link_libraries(holonight_system_storage PUBLIC Qt6::Core PRIVATE Qt6::DBus)
  target_compile_features(holonight_system_storage PUBLIC cxx_std_23)
  install(TARGETS holonight_system_storage EXPORT HoloNightSystemStorageTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(EXPORT HoloNightSystemStorageTargets FILE HoloNightSystemStorageTargets.cmake
    NAMESPACE HoloNightSystem:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoloNightSystemServices)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(BUILD_AUDIO)
  install(EXPORT HoloNightSystemAudioTargets
    FILE HoloNightSystemAudioTargets.cmake
    NAMESPACE HoloNightSystem::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoloNightSystemServices
  )
endif()
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
