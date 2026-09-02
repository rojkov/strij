load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

def strij_gateway_binary(
        name,
        extensions = [],
        deps = [],
        visibility = ["//visibility:private"],
        **kwargs):
    """Composes a gateway executable from the gateway framework and extensions.

    The framework provides `main()` and `RunGateway()`; each entry in
    `extensions` is a cc_library (declare `alwayslink = True`) that self-registers
    its factories via REGISTER_FACTORY at static init. Runtime selection stays
    config-driven (ExtensionConfig + typed_config).
    """
    cc_binary(
        name = name,
        deps = [
            "@strij//src/gateway/exe:gateway_framework",
        ] + list(extensions) + list(deps),
        visibility = visibility,
        **kwargs
    )

def strij_nodeagent_binary(
        name,
        extensions = [],
        deps = [],
        visibility = ["//visibility:private"],
        **kwargs):
    """Composes a node agent executable from the nodeagent framework and extensions.

    The framework provides `main()` and `RunNodeagent()`; each entry in
    `extensions` is a cc_library (declare `alwayslink = True`) that self-registers
    its factories via REGISTER_FACTORY at static init. Runtime selection stays
    config-driven (ExtensionConfig + typed_config).
    """
    cc_binary(
        name = name,
        deps = [
            "@strij//src/nodeagent/exe:nodeagent_framework",
        ] + list(extensions) + list(deps),
        visibility = visibility,
        **kwargs
    )