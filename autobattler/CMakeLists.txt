add_executable(autobattler main.c battle.c battle_view.c defs.c strings.c)
target_link_libraries(autobattler PRIVATE nv)
# Deployed apart from the editor app (docs/specs/battle.md, "Executable"): its own install component and a package of only
# what it reads, the UI font and its definition files.
nv_setup_executable(autobattler ROOT COMPONENT autobattler
    PRELOAD "${PROJECT_SOURCE_DIR}/assets/fonts@/assets/fonts" "${CMAKE_CURRENT_SOURCE_DIR}/data@/data")

# The build label names the commit and the build type (nv_version.h, NV_BUILD_NAME).
nv_add_version(autobattler)
