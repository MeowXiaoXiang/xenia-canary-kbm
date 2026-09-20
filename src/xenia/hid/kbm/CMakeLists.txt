add_library(xenia-hid-kbm-core STATIC kbm_binding.cc)
target_link_libraries(xenia-hid-kbm-core PUBLIC xenia-base)
xe_target_defaults(xenia-hid-kbm-core)

if(WIN32)
  add_library(xenia-hid-kbm STATIC kbm_config.cc kbm_hid.cc
                                        kbm_input_driver.cc)
  target_link_libraries(xenia-hid-kbm PUBLIC xenia-base xenia-hid
                        xenia-hid-kbm-core xenia-hid-keyboard xenia-ui)
  xe_target_defaults(xenia-hid-kbm)
endif()

if(XENIA_BUILD_TESTS)
  add_executable(xenia-hid-kbm-tests kbm_mouse_math_test.cc)
  target_compile_definitions(xenia-hid-kbm-tests PRIVATE CATCH_CONFIG_MAIN)
  target_include_directories(xenia-hid-kbm-tests PRIVATE
    ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(xenia-hid-kbm-tests PRIVATE cxx_std_17)
  target_link_libraries(xenia-hid-kbm-tests PRIVATE xenia-hid-kbm-core)
  add_test(NAME xenia-hid-kbm-tests COMMAND xenia-hid-kbm-tests)
endif()
