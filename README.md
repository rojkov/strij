# Scaffold for C++ projects using Bazel as the main build system.

Given you've got a fresh VM or a new Linux PC with Docker installed
for development just type in:

```bash
$ make env
```

After a moment or two you'll get into a brand new build environment
inside a Docker container.

Then check the initial fake targets build successfully:

```bash
$ make build
```

and the tests pass

```bash
$ make test
```

Now feel free to replace the fake bazel build targets with your own
real ones and build `compile_commands.json` to help your IDE do
auto-complete:

```bash
$ make compiledb
```

## Toolchain selection

By default the host compiler is used which is *gcc* usually. If you want to build
with the custom local *clang* toolchain add the following option to your build command:
`--extra_toolchains=//toolchain:cc_toolchain_for_linux_x86_64` or edit `.bazelrc`.

But first you need to get the toolchain. Two options:

  1. unpack a binary distribution of clang and llvm tools to `./toolchain/clang`;
  2. clone the sources of the LLVM project somewhere and enter:

```bash
$ make llvm-toolchain LLVM_PROJECT_PATH=/path/to/repo/with/llvm-project
```

By default `LLVM_PROJECT_PATH` is set to `/path/to/this/repo/../llvm-project`.

With the clang toolchain available you can run tests with address and thread
sanitizers enabled:

```bash
$ make test_asan
$ make test_tsan
```

As well as `clang-tidy` checks:

```bash
$ make clang-tidy
```

## Code coverage

Run tests instrumentalized for collecting coverage statistics:

```bash
$ make coverage
```

The code coverage report should be in `./coverage` now.

## Configuration

Gateway and NodeAgent use YAML configuration files with layered overrides (defaults → YAML → env → CLI). See [docs/config.md](docs/config.md) for full documentation.

Quick start:

```bash
# Validate config without starting
./gateway --config_file config/gateway.yaml --validate_only

# Start with environment variable override
CARROT_GATEWAY_HTTP_LISTENER_PORT=9090 ./gateway

# Start with CLI flag override
./gateway --http_port=9090 --log_level=debug
```
