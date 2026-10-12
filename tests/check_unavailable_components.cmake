# SPDX-License-Identifier: GPL-3.0-or-later
# Isolate discovery to the installed provider rather than a host installation.
foreach(component IN LISTS UNAVAILABLE_COMPONENTS)
  set(source "${TEST_BINARY_DIR}/unavailable-${component}")
  file(MAKE_DIRECTORY "${source}")
  file(WRITE "${source}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\nproject(unavailable LANGUAGES CXX)\nfind_package(HoloNightSystemServices REQUIRED COMPONENTS ${component})\n")
  execute_process(COMMAND ${CMAKE_COMMAND} -S "${source}" -B "${source}/build"
    "-DHoloNightSystemServices_DIR=${TEST_BINARY_DIR}/prefix/lib/cmake/HoloNightSystemServices"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Unavailable component: ${component}")
    message(FATAL_ERROR "Unavailable ${component} request did not fail correctly: ${output}${error}")
  endif()
endforeach()
