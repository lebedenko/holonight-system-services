add_executable(holonight_system_audio_tests
  main.cpp
  test_audio_controller.cpp
  test_audio_device_model.cpp
  test_audio_stream_model.cpp
  test_pulse_audio_backend.cpp
)
target_include_directories(holonight_system_audio_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/audio)
target_link_libraries(holonight_system_audio_tests PRIVATE
  HoloNightSystem::Audio Qt6::Test GTest::gtest GTest::gmock PkgConfig::LIBPULSE
)
add_test(NAME holonight_system_audio_tests COMMAND holonight_system_audio_tests)

add_test(NAME holonight_system_audio_install_consumer
  COMMAND ${CMAKE_COMMAND}
    -DPROVIDER_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-consumer
    -DCMAKE_GENERATOR=${CMAKE_GENERATOR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_install_consumer.cmake
)
