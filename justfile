# The native builds. Windows (docs/specs/d3d12_port.md): the Visual Studio generator, every configuration in one build folder.
# Linux: Ninja, a folder for each configuration (build-linux/Debug, build-linux/Release) and the downloaded tools shared between
# them. `just` lists the recipes of the system it runs on; `just build Release`, `just test Release`, `just run sandbox` (Windows).

set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

build_dir := "build-win"
generator := "Visual Studio 18 2026"
linux_build_dir := "build-linux"
web_build_dir := "build-web"

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

# Linux: run the tests of one configuration (the C tests natively, the .mjs ones with Node when there is one).
[linux]
test config="Debug": (build config)
    ctest --test-dir {{linux_build_dir}}/{{config}} --output-on-failure

# Linux: remove one configuration's build output (the configuration and the downloaded tools stay).
[linux]
clean config="Debug":
    cmake --build {{linux_build_dir}}/{{config}} --target clean

# The web build (Emscripten, Ninja; README.md): a folder for each configuration (build-web/Debug, build-web/Release) and the
# downloaded tools shared between them. Configured on the first `just web`.
[private]
[unix]
_web-configure config:
    [ -f {{web_build_dir}}/{{config}}/CMakeCache.txt ] || emcmake cmake -S . -B {{web_build_dir}}/{{config}} -G Ninja -DCMAKE_BUILD_TYPE={{config}} -DNV_TOOLS_DIR={{justfile_directory()}}/{{web_build_dir}}/tools

[private]
[windows]
_web-configure config:
    if (-not (Test-Path {{web_build_dir}}/{{config}}/CMakeCache.txt)) { emcmake cmake -S . -B {{web_build_dir}}/{{config}} -G Ninja -DCMAKE_BUILD_TYPE={{config}} -DNV_TOOLS_DIR={{justfile_directory()}}/{{web_build_dir}}/tools }

# Build the web sandbox and serve it on http://localhost:<port>/ with the development server (docs/specs/dev_server.md).
web config="Debug" port="8000": (_web-configure config)
    cmake --build {{web_build_dir}}/{{config}}
    node tools/dev_server.mjs --build {{web_build_dir}}/{{config}} --root {{web_build_dir}}/{{config}}/sandbox --port {{port}}

# Build a sample and serve it on http://localhost:<port>/ (docs/specs/samples.md). Only "stress" exists for now.
sample-web app="stress" config="Debug" port="8000": (_web-configure config)
    @if [ "{{app}}" != "stress" ]; then echo 'unknown sample "{{app}}" (only "stress" for now)'; exit 1; fi
    cmake --build {{web_build_dir}}/{{config}} --target {{app}}
    node tools/dev_server.mjs --build {{web_build_dir}}/{{config}} --root {{web_build_dir}}/{{config}}/samples/{{app}} --port {{port}}

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
