add_library(xenia-hid-kbm-core STATIC kbm_binding.cc kbm_controller_state.cc
                                        kbm_input_report.cc
                                        kbm_input_state.cc
                                        kbm_mouse_processor.cc)
target_link_libraries(xenia-hid-kbm-core PUBLIC xenia-base)
xe_target_defaults(xenia-hid-kbm-core)

if(WIN32)
  add_library(xenia-hid-kbm STATIC kbm_config.cc kbm_hid.cc
                                        kbm_input_adapter.cc
                                        kbm_input_driver.cc
                                        kbm_platform_input_win.cc)
  target_link_libraries(xenia-hid-kbm PUBLIC xenia-base xenia-hid
                        xenia-hid-kbm-core xenia-hid-keyboard xenia-ui)
  xe_target_defaults(xenia-hid-kbm)
endif()

if(XENIA_BUILD_TESTS)
  add_executable(xenia-hid-kbm-core-tests kbm_mouse_math_test.cc)
  target_compile_definitions(xenia-hid-kbm-core-tests PRIVATE
    CATCH_CONFIG_MAIN)
  target_include_directories(xenia-hid-kbm-core-tests PRIVATE
    ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(xenia-hid-kbm-core-tests PRIVATE cxx_std_17)
  target_link_libraries(xenia-hid-kbm-core-tests PRIVATE xenia-hid-kbm-core)
  add_test(NAME xenia-hid-kbm-core-tests COMMAND xenia-hid-kbm-core-tests)

  if(WIN32)
    add_executable(xenia-hid-kbm-config-tests kbm_config_test.cc)
    target_compile_definitions(xenia-hid-kbm-config-tests PRIVATE
      CATCH_CONFIG_MAIN)
    target_include_directories(xenia-hid-kbm-config-tests PRIVATE
      ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(xenia-hid-kbm-config-tests PRIVATE cxx_std_17)
    target_link_libraries(xenia-hid-kbm-config-tests PRIVATE xenia-hid-kbm)
    add_test(NAME xenia-hid-kbm-config-tests
      COMMAND xenia-hid-kbm-config-tests)
  endif()
endif()
