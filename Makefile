MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

BASENAME = $(shell basename ${MAKEFILE_DIR})
LLVM_PROJECT_PATH ?= $(shell realpath ${MAKEFILE_DIR}/../llvm-project)
LLVM_PATH = ${MAKEFILE_DIR}toolchain/clang
BAZELISK_VERSION = 1.28.1
BAZELISK_URL = https://github.com/bazelbuild/bazelisk/releases/download/v${BAZELISK_VERSION}/bazelisk-amd64.deb

build:
	bazel build //src/...

compiledb:
	bazel run @hedron_compile_commands//:refresh_all

coverage:
	bazel coverage //src/...
	./tools/generate_coverage.sh bazel-out/_coverage/_coverage_report.dat

test:
	bazel test //src/...

toolchain/abs_path.bzl:
	echo "module_abs_path = \"${MAKEFILE_DIR}\"" > $@

test_asan: toolchain/abs_path.bzl
	bazel test --config=asan //src/...

test_tsan: toolchain/abs_path.bzl
	bazel test -s --config=tsan //src/...

clang-tidy: toolchain/abs_path.bzl
	./tools/run-clang-tidy.py \
		-p=. \
		-config-file=.clang-tidy \
		-clang-tidy-binary=${LLVM_PATH}/bin/clang-tidy \
		-source-filter=".*${BASENAME}/(src|exe).*" \
		-clang-apply-replacements-binary=${LLVM_PATH}/bin/clang-apply-replacements \
		-use-color

docker/artifacts/bazelisk-amd64.deb:
	mkdir -p docker/artifacts
	wget -O docker/artifacts/bazelisk-amd64.deb ${BAZELISK_URL}

env: docker/artifacts/bazelisk-amd64.deb
	./docker/runenv.sh ${BASENAME}-dev

llvm-toolchain:
	./tools/build-llvm-project.sh ${LLVM_PROJECT_PATH}

.PHONY: build compiledb coverage test test_asan test_tsan clang-tidy env
