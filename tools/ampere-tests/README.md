# Ampere lowering quick tests

This standalone target compiles the existing Yoshi compute, geometry-exit and compute-lowering
tests against the production Ampere headers. It fetches only Catch2, using the version and
SHA-512 from the repository's `cpmfile.json`. It does not configure the emulator or link Qt,
audio, networking, Vulkan, the shader backend or the emulator core.

Requirements: CMake 3.24 or newer, a C++20 compiler and its standard library, and a build tool
such as Ninja or MSBuild. The first configure needs access to the pinned Catch2 archive.
Windows requires a Visual Studio C++ developer environment (compiler, Windows SDK and C++
standard library); clang-cl can use that same environment. No Python is required.

From the repository root:

```sh
cmake -S tools/ampere-tests -B build/ampere-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ampere-tests --target ampere-quick-tests --parallel 4
ctest --test-dir build/ampere-tests --output-on-failure
```

For an existing Visual Studio 2022 installation, a developer command prompt can instead use:

```bat
cmake -S tools/ampere-tests -B build/ampere-tests-vs -G "Visual Studio 17 2022" -A x64
cmake --build build/ampere-tests-vs --config Release --target ampere-quick-tests --parallel 4
ctest --test-dir build/ampere-tests-vs -C Release --output-on-failure
```

Run a subset with Catch2 filters, for example:

```sh
./build/ampere-tests/ampere-quick-tests "[yoshi]"
./build/ampere-tests/ampere-quick-tests "*geometry exit*"
```

An empty selection returns an error, including when `--allow-running-no-tests` is supplied.
CTest also verifies that contract. Run CTest with `--no-tests=error` when adding a CTest `-R`
filter, so an empty selection of CTest entries also fails.

Reuse the build directory for incremental runs. For a previously downloaded Catch2 source
tree, `-DFETCHCONTENT_SOURCE_DIR_CATCH2=/absolute/path/to/Catch2` permits offline configuration;
that override is operator supplied and bypasses archive hash verification, so use a source
tree already verified against the repository pin.

These checks cover lowering and its register/predicate rules. Keep the existing full test,
SPIR-V validation, emulator build and gameplay checks: passing this target does not establish
correct Vulkan output or playability.

See [VALIDATION.md](VALIDATION.md) for the initial local toolchain, measured runs and limits.
