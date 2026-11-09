import sys
import unittest

from strij import _invocation
from strij._runtime import RemoteRef

STATUS_OK = _invocation.STATUS_OK
STATUS_MALFORMED = _invocation.STATUS_MALFORMED_REQUEST
STATUS_NOT_FOUND = _invocation.STATUS_NOT_FOUND
STATUS_INTERNAL = _invocation.STATUS_INTERNAL


def _make_ref() -> RemoteRef:
    return RemoteRef(runtime=object(), call_id="ref")


class PickleModeTest(unittest.TestCase):
    def test_round_trips_callable_and_arguments(self) -> None:
        def add(left, right=1):
            return left + right

        body = _invocation.serialize_invocation(add, (2,), {"right": 3})
        result, status, reason = _invocation.execute_pickle(body)
        self.assertEqual(status, STATUS_OK, reason)
        import cloudpickle

        self.assertEqual(cloudpickle.loads(result), 5)

    def test_rejects_unsupported_version(self) -> None:
        import cloudpickle

        body = cloudpickle.dumps(
            {"__strij_invocation__": 999, "function": len, "args": (), "kwargs": {}}
        )
        result, status, reason = _invocation.execute_pickle(body)
        self.assertEqual(status, STATUS_MALFORMED)
        self.assertIn("999", reason)
        self.assertIn("supports 1", reason)
        self.assertEqual(result, b"")

    def test_rejects_unpicklable_body(self) -> None:
        _, status, reason = _invocation.execute_pickle(b"not a pickle")
        self.assertEqual(status, STATUS_MALFORMED)
        self.assertIn("Invocation", reason)

    def test_rejects_mismatched_client_python_before_unpickling(self) -> None:
        other_minor = sys.version_info[1] + 1
        header = _invocation._WIRE_MAGIC + (
            "%d.%d" % (sys.version_info[0], other_minor)
        ).encode("ascii")
        # The payload is deliberately not a pickle: the version check must run
        # first, so a mismatched client can never crash the worker.
        result, status, reason = _invocation.execute_pickle(header + b"\nnot a pickle")
        self.assertEqual(status, STATUS_MALFORMED)
        self.assertIn("matching Python versions", reason)
        self.assertEqual(result, b"")

    def test_python_exception_maps_to_internal(self) -> None:
        def explode():
            raise ValueError("boom")

        body = _invocation.serialize_invocation(explode, (), {})
        _, status, reason = _invocation.execute_pickle(body)
        self.assertEqual(status, STATUS_INTERNAL)
        self.assertIn("ValueError: boom", reason)
        self.assertIn("Traceback", reason)

    def test_unpicklable_result_maps_to_internal(self) -> None:
        import threading

        def returns_lock():
            return threading.Lock()

        body = _invocation.serialize_invocation(returns_lock, (), {})
        _, status, reason = _invocation.execute_pickle(body)
        self.assertEqual(status, STATUS_INTERNAL)
        self.assertIn("result", reason)

    def test_returning_remote_ref_is_rejected(self) -> None:
        def returns_ref():
            return _make_ref()

        body = _invocation.serialize_invocation(returns_ref, (), {})
        _, status, reason = _invocation.execute_pickle(body)
        self.assertEqual(status, STATUS_INTERNAL)
        self.assertIn("RemoteRef", reason)


class JsonModeTest(unittest.TestCase):
    def test_embedded_entry_with_input(self) -> None:
        body = b'{"python_code": "def main(value):\\n    return value * 2", "input": 21}'
        result, status, reason = _invocation.execute_json("embedded:main", body)
        self.assertEqual(status, STATUS_OK, reason)
        self.assertEqual(result, b"42")

    def test_args_and_kwargs_binding(self) -> None:
        body = (
            b'{"python_code": "def main(a, b=0):\\n    return a + b",'
            b' "args": [1], "kwargs": {"b": 2}}'
        )
        result, status, reason = _invocation.execute_json("embedded:main", body)
        self.assertEqual(status, STATUS_OK, reason)
        self.assertEqual(result, b"3")

    def test_whole_body_is_the_argument(self) -> None:
        body = (
            b'{"python_code": "def main(payload):\\n    return sorted(payload)", '
            b'"b": 2, "a": 1}'
        )
        result, status, reason = _invocation.execute_json("embedded:main", body)
        self.assertEqual(status, STATUS_OK, reason)
        self.assertEqual(result, b'["a", "b"]')

    def test_input_with_args_is_malformed(self) -> None:
        body = b'{"python_code": "def main(*args):\\n    return 0", "input": 1, "args": [2]}'
        _, status, reason = _invocation.execute_json("embedded:main", body)
        self.assertEqual(status, STATUS_MALFORMED)
        self.assertIn("mix", reason)

    def test_invalid_json_is_malformed(self) -> None:
        _, status, reason = _invocation.execute_json("embedded:main", b"{not json")
        self.assertEqual(status, STATUS_MALFORMED)
        self.assertIn("JSON", reason)

    def test_missing_entry_is_not_found(self) -> None:
        body = b'{"python_code": "def other():\\n    return 1", "input": 1}'
        _, status, reason = _invocation.execute_json("embedded:main", body)
        self.assertEqual(status, STATUS_NOT_FOUND)
        self.assertIn("main", reason)

    def test_module_reference_resolves_importable_callable(self) -> None:
        # json.dumps(1) -> "1" (the JSON result is the string "1").
        result, status, reason = _invocation.execute_json(
            "module:json:dumps", b'{"args": [1]}'
        )
        self.assertEqual(status, STATUS_OK, reason)
        self.assertEqual(result, b'"1"')

    def test_unresolvable_module_is_not_found(self) -> None:
        _, status, reason = _invocation.execute_json(
            "module:definitely_not_a_module_xyz:main", b"{}"
        )
        self.assertEqual(status, STATUS_NOT_FOUND)

    def test_non_json_result_is_internal(self) -> None:
        body = b'{"python_code": "def main():\\n    return object()", "args": []}'
        _, status, reason = _invocation.execute_json("embedded:main", body)
        self.assertEqual(status, STATUS_INTERNAL)
        self.assertIn("JSON", reason)

    def test_unknown_reference_is_malformed(self) -> None:
        _, status, _ = _invocation.execute_json("http://example", b"{}")
        self.assertEqual(status, STATUS_MALFORMED)


if __name__ == "__main__":
    unittest.main()
