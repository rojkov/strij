"""
This file contains common build rules for the Carrot project.
"""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Transform the package path (e.g. include/carrot/common) into a path for
# the include prefix (e.g. carrot/common). This allows us to
# write #include "carrot/common/pure.hh" instead of #include "include/carrot/common/pure.hh".
def carrot_include_prefix(path):
    if path.startswith("include/") or path.startswith("src/"):
        return "/".join(path.split("/")[1:])
    return None

def visibility_for_tests(path):
    if path.startswith("src/"):
        return ["//test/" + "/".join(path.split("/")[1:]) + ":__pkg__"]
    return []

def carrot_cc_binary(
        name,
        srcs = [],
        deps = [],
        visibility = ["//visibility:private"]):
    cc_binary(
        name = name,
        srcs = srcs,
        deps = deps,
        visibility = visibility,
    )

def carrot_cc_library(
        name,
        srcs = [],
        hdrs = [],
        deps = [],
        visibility = ["//visibility:private"]):
    print(visibility_for_tests(native.package_name()))
    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        visibility = visibility + visibility_for_tests(native.package_name()),
        include_prefix = carrot_include_prefix(native.package_name()),
    )

def carrot_cc_test_library(
        name,
        srcs = [],
        hdrs = [],
        deps = [],
        visibility = ["//visibility:public"]):
    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = ["@googletest//:gtest"] + deps,
        visibility = visibility,
    )

def carrot_cc_test(
        name,
        srcs = [],
        deps = []):
    cc_test(
        name = name,
        srcs = srcs,
        deps = ["@googletest//:gtest"] + deps,
        visibility = ["//visibility:private"],
    )
