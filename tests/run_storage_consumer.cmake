# Each phase is checked; no host-installed provider is used.
function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Storage install consumer command failed: ${ARGV}")
  endif()
endfunction()
run(${CMAKE_COMMAND} -S "${PROVIDER_SOURCE_DIR}" -B "${TEST_BINARY_DIR}/provider"
  -DBUILD_TESTS=OFF -DBUILD_AUDIO=OFF -DBUILD_STORAGE=ON
  -DCMAKE_INSTALL_PREFIX=${TEST_BINARY_DIR}/prefix)
run(${CMAKE_COMMAND} --build "${TEST_BINARY_DIR}/provider" --parallel 2)
run(${CMAKE_COMMAND} --install "${TEST_BINARY_DIR}/provider")
run(${CMAKE_COMMAND} -S "${CMAKE_CURRENT_LIST_DIR}/storage-consumer" -B "${TEST_BINARY_DIR}/consumer"
  -DCMAKE_PREFIX_PATH=${TEST_BINARY_DIR}/prefix)
run(${CMAKE_COMMAND} --build "${TEST_BINARY_DIR}/consumer" --parallel 2)
