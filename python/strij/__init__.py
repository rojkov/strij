"""Public surface of the Strij Python SDK.

The same package is used in two contexts:

* inside a worker process started by the ``pyfunction`` task handler, where the
  bootstrap installs a pipe-backed runtime; and
* in a client process, where :func:`init` installs an HTTP-backed runtime.

Importing this package is deliberately cheap: cloudpickle and the generated
protobuf modules are imported lazily by the worker bootstrap, so the client
package and ``import strij`` work without either dependency installed.
"""

from ._invocation import InvocationError
from ._runtime import (
    RemoteError,
    RemoteRef,
    RemoteTimeoutError,
    TaskStatus,
    init,
    remote,
)

__all__ = [
    "InvocationError",
    "RemoteError",
    "RemoteRef",
    "RemoteTimeoutError",
    "TaskStatus",
    "init",
    "remote",
]
