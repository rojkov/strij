MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

BASENAME = $(shell basename ${MAKEFILE_DIR})
LLVM_PROJECT_PATH ?= $(shell realpath ${MAKEFILE_DIR}/../llvm-project)
LLVM_PATH = ${MAKEFILE_DIR}toolchain/clang
SANITIZER_SYMBOLIZER_FLAG = --test_env=ASAN_SYMBOLIZER_PATH=${LLVM_PATH}/bin/llvm-symbolizer
BAZELISK_VERSION = 1.28.1
BAZELISK_URL = https://github.com/bazelbuild/bazelisk/releases/download/v${BAZELISK_VERSION}/bazelisk-amd64.deb
BAZEL_FLAGS ?=

build:
	bazel build $(BAZEL_FLAGS) //...

compiledb: build
	bazel run $(BAZEL_FLAGS) @hedron_compile_commands//:refresh_all
	bazel build $(BAZEL_FLAGS) //...

# Bazel's `coverage` toolchain feature instruments every C++ dependency,
# including external ones. Disable it and instrument only our own sources via
# --per_file_copt so third-party code (e.g. protobuf) is untouched.
coverage:
	bazel coverage $(BAZEL_FLAGS) \
		--features=-coverage \
		--instrumentation_filter='^//src/...' \
		--per_file_copt='^src/.*@-fprofile-arcs,-ftest-coverage' \
		--linkopt=--coverage //...
	./tools/generate_coverage.sh bazel-out/_coverage/_coverage_report.dat

test: test_consumer
	bazel test $(BAZEL_FLAGS) //...

# End-to-end tests that start real gateway/nodeagent processes. They skip when
# the host denies io_uring_setup. Included in `make test` via //...
test_e2e:
	bazel test $(BAZEL_FLAGS) //test/e2e/...

# Smoke-test consumability from a fresh external repo (test/consumer flips @strij
# into a real external module via MODULE.bazel + local_path_override). Must run
# before the root `bazel test //...` because root bazel owns the root workspace.
test_consumer:
	cd ${MAKEFILE_DIR}test/consumer && bazel build $(BAZEL_FLAGS) //... && bazel test $(BAZEL_FLAGS) //...

build_consumer:
	cd ${MAKEFILE_DIR}test/consumer && bazel build $(BAZEL_FLAGS) //...

toolchain/abs_path.bzl:
	echo "clang_abs_path = \"$(realpath ${LLVM_PATH})\"" > $@

test_asan: toolchain/abs_path.bzl
	bazel test $(BAZEL_FLAGS) $(SANITIZER_SYMBOLIZER_FLAG) --test_output=errors --config=asan //test/...

test_tsan: toolchain/abs_path.bzl
	bazel test $(BAZEL_FLAGS) $(SANITIZER_SYMBOLIZER_FLAG) --test_output=errors -s --config=tsan //test/...

clang-tidy: toolchain/abs_path.bzl
	./tools/run-clang-tidy.py \
		-p=. \
		-config-file=.clang-tidy \
		-clang-tidy-binary=${LLVM_PATH}/bin/clang-tidy \
		-source-filter=".*${BASENAME}/(src|exe).*" \
		-clang-apply-replacements-binary=${LLVM_PATH}/bin/clang-apply-replacements \
		-use-color

format:
	python3 tools/format_cpp.py

check_format:
	python3 tools/format_cpp.py --check

check_includes:
	python3 tools/check_include_purity.py

check_namespaces:
	python3 tools/check_namespace_coherence.py

check: check_includes check_namespaces

docker/artifacts/bazelisk-amd64.deb:
	mkdir -p docker/artifacts
	wget -O docker/artifacts/bazelisk-amd64.deb ${BAZELISK_URL}

env: docker/artifacts/bazelisk-amd64.deb
	./docker/runenv.sh ${BASENAME}-dev

llvm-toolchain:
	./tools/build-llvm-project.sh ${LLVM_PROJECT_PATH}

.PHONY: build compiledb coverage test test_e2e test_consumer build_consumer test_asan test_tsan clang-tidy format check_format check check_includes check_namespaces env
