if(BUILD_TEST)
  set(TORCH_BACKEND_CORE_TEST_SOURCES
    ${PROJECT_SOURCE_DIR}/test/cpp/common/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exception_test.cpp)

  add_executable(test_core ${TORCH_BACKEND_CORE_TEST_SOURCES})
  target_link_libraries(test_core PRIVATE torch_backend gtest_main gtest)
  add_test(NAME test_core COMMAND $<TARGET_FILE:test_core>)
endif()

if(INSTALL_TEST)
  install(TARGETS test_core DESTINATION bin)
  # Install PDB files for MSVC builds
  if(MSVC)
    install(FILES $<TARGET_PDB_FILE:test_core> DESTINATION bin OPTIONAL)
  endif()
endif()
