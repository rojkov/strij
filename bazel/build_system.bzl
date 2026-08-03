"""
This file contains common build rules for the Strij project.
"""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Transform the package path (e.g. include/strij/common) into a path for
# the include prefix (e.g. strij/common). This allows us to
# write #include "strij/common/pure.hh" instead of #include "include/strij/common/pure.hh".
def strij_include_prefix(path):
    if path.startswith("include/") or path.startswith("src/"):
        return "/".join(path.split("/")[1:])
    return None

def visibility_for_tests(path):
    if path.startswith("src/"):
        return ["//test/" + "/".join(path.split("/")[1:]) + ":__pkg__"]
    return []

def strij_cc_binary(
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

def strij_cc_library(
        name,
        srcs = [],
        hdrs = [],
        deps = [],
        visibility = ["//visibility:private"],
        alwayslink = False):
    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        visibility = visibility + visibility_for_tests(native.package_name()),
        include_prefix = strij_include_prefix(native.package_name()),
        alwayslink = alwayslink,
    )

def strij_cc_test_library(
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

def strij_cc_test(
        name,
        srcs = [],
        deps = []):
    cc_test(
        name = name,
        srcs = srcs,
        deps = ["@googletest//:gtest"] + deps,
        visibility = ["//visibility:private"],
    )
