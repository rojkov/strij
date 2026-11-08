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
        deps = [],
        data = []):
    cc_test(
        name = name,
        srcs = srcs,
        data = data,
        deps = ["@googletest//:gtest"] + deps,
        visibility = ["//visibility:private"],
    )

def strij_e2e_test(
        name,
        srcs = [],
        deps = [],
        data = [],
        size = "small"):
    """A test that drives the real gateway and nodeagent binaries.

    `strij_cc_test` accepts neither `tags` nor `timeout`, so end-to-end tests get
    their own sibling macro. The two binaries are always added as `data`
    dependencies (resolved through runfiles by the harness), and the network tag
    documents that the suite binds loopback listeners. The exclusive tag keeps
    clusters from different test binaries from racing for host resources or the
    same bind/close-allocated ephemeral port.
    """
    cc_test(
        name = name,
        srcs = srcs,
        data = data + [
            "//src/gateway/exe:gateway",
            "//src/nodeagent/exe:nodeagent",
        ],
        deps = ["@googletest//:gtest"] + deps,
        tags = ["requires-network", "exclusive"],
        timeout = "short",
        size = size,
        visibility = ["//visibility:private"],
    )
