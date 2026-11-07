# The native builds. Windows (docs/specs/d3d12_port.md): the Visual Studio generator, every configuration in one build folder.
# Linux: Ninja, a folder for each configuration (build-linux/Debug, build-linux/Release) and the downloaded tools shared between
# them. `just` lists the recipes of the system it runs on; `just build Release`, `just test Release`, `just run sandbox`.

set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

build_dir := "build-win"
generator := "Visual Studio 18 2026"
linux_build_dir := "build-linux"

default:
    @just --list

# Fetch the Git LFS assets.
lfs:
    git lfs pull

# Generate the Visual Studio solution (the build does this itself when the folder has none).
[windows]
configure:
    cmake -S . -B {{build_dir}} -G "{{generator}}" -A x64

# Build every target of one configuration (Debug or Release).
[windows]
build config="Debug":
    if (-not (Test-Path {{build_dir}}/CMakeCache.txt)) { just configure }
    cmake --build {{build_dir}} --config {{config}} -j

# Build, then run an executable (the sandbox); the arguments after the configuration go to it.
[windows]
run app="sandbox" config="Debug" *args: (build config)
    & {{build_dir}}/{{app}}/{{config}}/{{app}}.exe {{args}}

# Run a Debug build's script (engine/debug_script.h); exits with 1 when the log holds an error.
[windows]
script file out=(build_dir / "script") app="sandbox": (build "Debug")
    & {{build_dir}}/{{app}}/Debug/{{app}}.exe --script "{{file}}" --out "{{out}}"

# Run the C tests of one configuration.
[windows]
test config="Debug": (build config)
    ctest --test-dir {{build_dir}} -C {{config}} --output-on-failure

# Remove one configuration's build output (the solution and the downloaded tools stay).
[windows]
clean config="Debug":
    cmake --build {{build_dir}} --config {{config}} --target clean

# Linux: configure one configuration's folder (the build does this itself when the folder has none).
[linux]
configure config="Debug":
    cmake -S . -B {{linux_build_dir}}/{{config}} -G Ninja -DCMAKE_BUILD_TYPE={{config}} -DNV_TOOLS_DIR={{justfile_directory()}}/{{linux_build_dir}}/tools

# Linux: build every target of one configuration (Debug or Release).
[linux]
build config="Debug":
    [ -f {{linux_build_dir}}/{{config}}/CMakeCache.txt ] || just configure {{config}}
    cmake --build {{linux_build_dir}}/{{config}}

# Linux: build, then run an executable (the sandbox); the arguments after the configuration go to it.
[linux]
run app="sandbox" config="Debug" *args: (build config)
    {{linux_build_dir}}/{{config}}/{{app}}/{{app}} {{args}}

# Linux: run the tests of one configuration (the C tests natively, the .mjs ones with Node when there is one).
[linux]
test config="Debug": (build config)
    ctest --test-dir {{linux_build_dir}}/{{config}} --output-on-failure

# Linux: remove one configuration's build output (the configuration and the downloaded tools stay).
[linux]
clean config="Debug":
    cmake --build {{linux_build_dir}}/{{config}} --target clean

# The recipes below are the Linux visual regression test (docs/specs/visual_regression.md): the Wayland windowed build,
# run explicitly, never by CI.

# Build the visual test's app (Debug, build-visual/) and capture its cases into build-visual/visual-out/.
[private]
[unix]
_visual-capture:
    cmake -S . -B build-visual -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNV_HEADLESS=OFF -DNV_BUILD_VISUAL_TEST=ON
    cmake --build build-visual --target visual_test
    mkdir -p build-visual/visual-out
    build-visual/tests/visual/visual_test build-visual/visual-out

# Capture, then compare with the baselines in tests/visual/baseline (odiff; diff PNGs land beside the captures).
[unix]
visual: _visual-capture
    node tools/visual/compare.mjs --odiff build-visual/tools/odiff --baseline tests/visual/baseline --actual build-visual/visual-out

# Capture, then make the captures the baselines. View the change before committing what this writes.
[unix]
visual-update-ref: _visual-capture
    mkdir -p tests/visual/baseline
    for f in build-visual/visual-out/*.png; do case "$f" in *.diff.png) ;; *) cp "$f" tests/visual/baseline/ ;; esac; done
    @echo "baselines updated in tests/visual/baseline: view them, then commit"
