add_library(xenia-hid-kbm STATIC)
xe_platform_sources(xenia-hid-kbm ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xenia-hid-kbm PUBLIC xenia-base xenia-hid xenia-hid-keyboard xenia-ui)
xe_target_defaults(xenia-hid-kbm)
