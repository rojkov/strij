"""Generated protobuf modules shipped with the worker wheel.

These are vendored from the Bazel ``py_proto_library`` outputs so an installed
``strij`` package needs no Bazel runfiles or separate generated modules. When
``api/nodeagent/extensions/task_handlers/pyfunction/pyfunction.proto`` or
``api/common/node/capabilities.proto`` changes, regenerate them with::

    bazel build //api/nodeagent/extensions/task_handlers/pyfunction:pyfunction_py_pb2
    cp <pyfunction_pb2.py> python/strij/_proto/pyfunction_pb2.py
    cp <capabilities_pb2.py> python/strij/_proto/capabilities_pb2.py

The generated ``pyfunction_pb2.py`` imports its dependency as
``from common.node import capabilities_pb2``; rewrite that one line to
``from . import capabilities_pb2`` for the wheel-local package.
"""
