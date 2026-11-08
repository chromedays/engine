# The stress sample (docs/specs/samples.md). It packs only the assets it reads (the UI font and the character), not the whole
# assets/ directory.
add_executable(stress main.c workloads.c)
target_link_libraries(stress PRIVATE nv)
nv_setup_executable(stress
    PRELOAD "${PROJECT_SOURCE_DIR}/assets/fonts@/assets/fonts" "${PROJECT_SOURCE_DIR}/assets/quaternius@/assets/quaternius")

# The build label and the benchmark report name the commit the build comes from and its build type (nv_version.h, NV_BUILD_NAME).
nv_add_version(stress)
