# The visual regression test's capture app (docs/specs/visual_regression.md). The top level adds this
# directory only with NV_BUILD_VISUAL_TEST on the Linux windowed build; it is not a ctest test.

add_executable(visual_test visual_test.c)
target_include_directories(visual_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(visual_test PRIVATE nv)
