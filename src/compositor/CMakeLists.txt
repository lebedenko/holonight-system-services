find_package(Qt6 REQUIRED COMPONENTS Network)
file(GLOB public_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/holonight_system/compositor/*.h")
add_library(holonight_system_compositor STATIC ${public_headers}
  CompositorFactory.cpp WindowActivation.cpp HyprlandBackend.cpp HyprlandBackend.h
  HyprlandIpc.cpp HyprlandIpc.h HyprlandIpcClient.cpp HyprlandIpcClient.h
  SwayBackend.cpp SwayBackend.h SwayIpc.cpp SwayIpc.h)
add_library(HoloNightSystem::Compositor ALIAS holonight_system_compositor)
target_compile_features(holonight_system_compositor PUBLIC cxx_std_23)
set_target_properties(holonight_system_compositor PROPERTIES EXPORT_NAME Compositor POSITION_INDEPENDENT_CODE ON)
target_include_directories(holonight_system_compositor PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/holonight_system/compositor>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/holonight_system/compositor>)
target_link_libraries(holonight_system_compositor PUBLIC Qt6::Core PRIVATE Qt6::Network)
if(BUILD_COMPOSITOR_WAYLAND)
  find_package(Qt6 REQUIRED COMPONENTS Gui WaylandClient)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  target_sources(holonight_system_compositor PRIVATE GenericBackend.cpp GenericBackend.h
    GenericWorkspaceState.h LabwcBackend.cpp LabwcBackend.h ForeignToplevelState.h)
  target_compile_definitions(holonight_system_compositor PRIVATE HOLONIGHT_COMPOSITOR_WAYLAND)
  target_link_libraries(holonight_system_compositor PRIVATE Qt6::Gui Qt6::WaylandClient PkgConfig::WAYLAND_CLIENT)
  qt6_generate_wayland_protocol_client_sources(holonight_system_compositor FILES
    ${PROJECT_SOURCE_DIR}/protocols/ext-workspace-v1.xml
    ${PROJECT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml)
endif()
install(TARGETS holonight_system_compositor EXPORT HoloNightSystemCompositorTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT HoloNightSystemCompositorTargets FILE HoloNightSystemCompositorTargets.cmake
  NAMESPACE HoloNightSystem:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoloNightSystemServices)
