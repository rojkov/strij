import threading
import unittest
from types import SimpleNamespace

import cloudpickle

import strij
from strij import _invocation, _runtime


class CapturingRuntime(_runtime._Runtime):
    def __init__(self) -> None:
        super().__init__()
        self.payloads = []
        self.requirements = []

    def _submit_serialized(self, payload, requirements, timeout):
        self.payloads.append(payload)
        self.requirements.append(dict(requirements))
        ref = _runtime.RemoteRef(self, "captured")
        ref._resolve("sent")
        return ref


class RemoteRefTest(unittest.TestCase):
    def test_get_is_idempotent(self) -> None:
        ref = _runtime.RemoteRef(object(), "id")
        ref._resolve({"value": 1})
        self.assertEqual(ref.get(), {"value": 1})
        self.assertEqual(ref.get(), {"value": 1})

    def test_timeout_is_distinct_from_remote_error(self) -> None:
        ref = _runtime.RemoteRef(object(), "id")
        with self.assertRaises(_runtime.RemoteTimeoutError):
            ref.get(timeout=0.01)
        # The ref remains pending and can still resolve later.
        ref._resolve(2)
        self.assertEqual(ref.get(), 2)

    def test_failure_is_sticky(self) -> None:
        ref = _runtime.RemoteRef(object(), "id")
        error = _runtime.RemoteError(_runtime.TaskStatus.INTERNAL, "boom")
        ref._reject(error)
        with self.assertRaises(_runtime.RemoteError):
            ref.get()
        with self.assertRaises(_runtime.RemoteError):
            ref.get()


class RuntimeSelectionTest(unittest.TestCase):
    def tearDown(self) -> None:
        _runtime._set_runtime(None)

    def test_missing_runtime_reports_clearly(self) -> None:
        _runtime._set_runtime(None)

        @strij.remote
        def double(value):
            return value * 2

        with self.assertRaisesRegex(RuntimeError, "no Strij runtime"):
            double.remote(2)

    def test_normal_call_executes_locally(self) -> None:
        calls = []

        @strij.remote
        def double(value):
            calls.append(value)
            return value * 2

        self.assertEqual(double(3), 6)
        self.assertEqual(calls, [3])


class EagerResolutionTest(unittest.TestCase):
    def test_direct_and_nested_refs_are_resolved_before_serialization(self) -> None:
        runtime = CapturingRuntime()
        direct = _runtime.RemoteRef(runtime, "a")
        direct._resolve(10)
        nested = _runtime.RemoteRef(runtime, "b")
        nested._resolve(20)

        def consume(*args, **kwargs):
            return args, kwargs

        runtime.submit(consume, ([direct, {"n": nested}],), {"k": direct}, None, None)
        payload = _invocation.deserialize_invocation(runtime.payloads[0])
        self.assertEqual(payload.args, ([10, {"n": 20}],))
        self.assertEqual(payload.kwargs, {"k": 10})
        self.assertEqual(payload.version, _invocation.INVOCATION_VERSION)

    def test_foreign_ref_is_rejected_before_submission(self) -> None:
        runtime = CapturingRuntime()
        other = CapturingRuntime()
        foreign = _runtime.RemoteRef(other, "foreign")
        with self.assertRaisesRegex(RuntimeError, "different Strij runtime"):
            runtime.submit(len, (foreign,), {}, None, None)
        self.assertEqual(runtime.payloads, [])

    def test_requirements_and_timeout_are_carried(self) -> None:
        runtime = CapturingRuntime()
        runtime.submit(len, ([1],), {}, {"cpu": 2}, 3.0)
        self.assertEqual(runtime.requirements[0], {"cpu": 2})


class WorkerRuntimeTest(unittest.TestCase):
    def test_success_result_resolves_ref(self) -> None:
        sent = []
        runtime = _runtime.WorkerRuntime(lambda call_id, body, requirements: sent.append(call_id))
        ref = runtime.submit(len, ([1, 2]), {}, None, None)
        self.assertEqual(len(sent), 1)

        frame = SimpleNamespace(
            remote_result=SimpleNamespace(
                call_id=sent[0],
                body=cloudpickle.dumps(2),
                is_final=True,
                status=0,
                reason="",
            )
        )
        runtime.on_remote_result(frame)
        self.assertEqual(ref.get(), 2)

    def test_chunked_result_is_accumulated(self) -> None:
        sent = []
        runtime = _runtime.WorkerRuntime(lambda call_id, body, requirements: sent.append(call_id))
        ref = runtime.submit(len, ([1]), {}, None, None)

        whole = cloudpickle.dumps("chunked")
        split = len(whole) // 2

        def frame(body, final):
            return SimpleNamespace(
                remote_result=SimpleNamespace(
                    call_id=sent[0], body=body, is_final=final, status=0, reason=""
                )
            )

        runtime.on_remote_result(frame(whole[:split], False))
        runtime.on_remote_result(frame(whole[split:], True))
        self.assertEqual(ref.get(), "chunked")

    def test_error_result_maps_to_remote_error(self) -> None:
        sent = []
        runtime = _runtime.WorkerRuntime(lambda call_id, body, requirements: sent.append(call_id))
        ref = runtime.submit(len, ([1]), {}, None, None)
        frame = SimpleNamespace(
            remote_result=SimpleNamespace(
                call_id=sent[0],
                body=b"",
                is_final=True,
                status=_runtime.TaskStatus.CAPACITY_REFUSED,
                reason="no capacity",
            )
        )
        runtime.on_remote_result(frame)
        with self.assertRaises(_runtime.RemoteError) as raised:
            ref.get()
        self.assertEqual(raised.exception.status, _runtime.TaskStatus.CAPACITY_REFUSED)
        self.assertIn("no capacity", str(raised.exception))

    def test_concurrent_submissions_resolve_on_their_threads(self) -> None:
        sent = []
        sent_lock = threading.Lock()
        barrier = threading.Barrier(5)

        def send_call(call_id, body, requirements):
            with sent_lock:
                sent.append(call_id)
            barrier.wait()

        runtime = _runtime.WorkerRuntime(send_call)
        results = []
        errors = []

        def worker(value):
            try:
                ref = runtime.submit(len, ([value]), {}, None, None)
                results.append(ref.get())
            except Exception as exc:  # noqa: BLE001
                errors.append(exc)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
        for thread in threads:
            thread.start()
        barrier.wait()

        for call_id in list(sent):
            runtime.on_remote_result(
                SimpleNamespace(
                    remote_result=SimpleNamespace(
                        call_id=call_id,
                        body=cloudpickle.dumps(1),
                        is_final=True,
                        status=0,
                        reason="",
                    )
                )
            )
        for thread in threads:
            thread.join()

        self.assertEqual(errors, [])
        self.assertEqual(sorted(results), [1, 1, 1, 1])


if __name__ == "__main__":
    unittest.main()
