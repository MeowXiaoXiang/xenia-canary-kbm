add_library(xenia-hid-kbm STATIC)
xe_platform_sources(xenia-hid-kbm ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xenia-hid-kbm PUBLIC xenia-base xenia-hid xenia-hid-keyboard xenia-ui)
xe_target_defaults(xenia-hid-kbm)

if(XENIA_BUILD_TESTS)
  add_executable(xenia-hid-kbm-tests kbm_mouse_math_test.cc)
  target_compile_definitions(xenia-hid-kbm-tests PRIVATE CATCH_CONFIG_MAIN)
  target_include_directories(xenia-hid-kbm-tests PRIVATE
    ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(xenia-hid-kbm-tests PRIVATE cxx_std_17)
  add_test(NAME xenia-hid-kbm-tests COMMAND xenia-hid-kbm-tests)
endif()
