function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Compositor install consumer failed: ${ARGV}")
  endif()
endfunction()
run(${CMAKE_COMMAND} -S "${PROVIDER_SOURCE_DIR}" -B "${TEST_BINARY_DIR}/provider" -G Ninja
  -DBUILD_TESTS=OFF -DBUILD_AUDIO=OFF -DBUILD_STORAGE=OFF -DBUILD_COMPOSITOR=ON
  -DBUILD_COMPOSITOR_WAYLAND=${COMPOSITOR_WAYLAND} -DCMAKE_INSTALL_PREFIX=${TEST_BINARY_DIR}/prefix)
run(${CMAKE_COMMAND} --build "${TEST_BINARY_DIR}/provider" --parallel 4)
run(${CMAKE_COMMAND} --install "${TEST_BINARY_DIR}/provider")
run(${CMAKE_COMMAND} -S "${CMAKE_CURRENT_LIST_DIR}/compositor-consumer" -B "${TEST_BINARY_DIR}/consumer" -G Ninja
  -DCMAKE_PREFIX_PATH=${TEST_BINARY_DIR}/prefix)
run(${CMAKE_COMMAND} --build "${TEST_BINARY_DIR}/consumer")
run("${TEST_BINARY_DIR}/consumer/compositor-consumer")
