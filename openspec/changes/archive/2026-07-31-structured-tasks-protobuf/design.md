## Context

Today the gateway–nodeagent task protocol is a hand-rolled binary value inside a TLV frame: `kTaskSubmission` and `kResult` values are `[task_id: 8 bytes][payload: N bytes]` in native byte order. The nodeagent is a hardcoded echo server — it never interprets the value. Protobuf is already used in the repo, but only for configuration (`api/core/config/`), not for the data plane.

This change replaces the opaque task value with a Protobuf-encoded `Task { id, type, body }`, so the `type` field becomes first-class and future nodeagent handler extensions can dispatch on it. The HTTP request path (`POST /tasks/{type}`) becomes the source of the task type.

## Goals / Non-Goals

**Goals:**
- Define a stable, self-describing wire format for tasks (`Task`) and results (`TaskResult`) using Protobuf.
- Extract the task `type` from the HTTP URL path at the gateway (`POST /tasks/{type}`), with sensible error responses (404 / 400).
- Make `LlhttpParser` deliver path + body (an `HttpRequest` struct), matching the existing "parser delivers a parsed artifact" pattern of `TlvParser` → `TlvFrame`.
- Keep the TLV framing layer unchanged; only the value content changes.
- Keep the nodeagent's behavior functionally identical (echo), but operating on the new protobuf format.

**Non-Goals:**
- Nodeagent task-handler dispatch / pluggable handler extensions (separate proposal).
- Gateway-side validation of unknown task types (deferred until `NodeDirectory` advertises supported types; then "no Node available" → 503 path already exists).
- `GET /tasks` listing endpoint.
- Any other protocol evolution (timeouts, retries, flow control).

## Decisions

### 1. Protobuf message schemas

`api/core/task/task.proto`, package `strij.task`:

```protobuf
message Task {
  uint64 id = 1;     // gateway-assigned, routes the result back to the HTTP client
  string type = 2;   // task handler selector, e.g. "echo"; from URL path
  bytes body = 3;    // task payload (the HTTP request body)
}

message TaskResult {
  uint64 id = 1;     // echoes the originating Task.id
  bytes body = 2;    // result payload
}
```

Rationale:
- **`type` is a `string`, not an enum** — task types come from URL paths and will be registered by pluggable handler extensions. An enum would require recompiling the proto for each new handler. The gateway does not validate it for now (see Non-Goals).
- **`TaskResult` carries only `id` + `body`** — the gateway routes results exclusively by `id` via `ResultReceiverStorage`; a `type` on the result is not needed yet. Adding it later is a non-breaking proto evolution.
- **`bytes` for bodies** — the HTTP body is arbitrary bytes; `string` would imply UTF-8.

### 2. Proto build layout

New package `api/core/task/` with `task.proto`, `proto_library`, and `cc_proto_library` targets, mirroring `api/core/config/BUILD.bazel`. Include prefix `core/task` via the repo's auto-derived include-prefix rule (`api/core/task` → `core/task`). Generated headers are consumed by gateway and nodeagent targets.

### 3. `LlhttpParser` delivers `HttpRequest`

Add a struct and change the callback signature (same pattern as `TlvParser` → `TlvFrame`):

```cpp
struct HttpRequest {
  std::string_view path;                 // e.g. "/tasks/echo"
  std::span<const std::byte> body;       // request body
};
// callback becomes std::move_only_function<void(HttpRequest)>
```

- Register llhttp `on_url` (same `llhttp_data_cb` shape as `on_body`). The URL may arrive fragmented across reads, so accumulate fragments into a `std::string` member `path_`; at message completion, hand out a `string_view` into it. The body span continues to point into the parser's own `Chunk` memory (zero-copy, unchanged).
- The `path` string_view and body span remain valid for the duration of the callback invocation — the handler must copy/serialize before returning. This is already the implicit contract for the body span.

### 4. TLV framing retained; value becomes serialized protobuf

Keep `TlvFrame`, `SerializeTlvFrame`, `TlvParser`, and the type constants unchanged. Only the value content changes:

```
kTaskSubmission (0): [type:1][len:4][ serialized strij.task.Task  ]
kResult         (1): [type:1][len:4][ serialized strij.task.TaskResult ]
kHeartbeat      (2): [type:1][len:4][ empty ]
```

Alternatives considered:
- **Replace TLV with pure protobuf framing** (each frame a length-delimited message): more disruptive; TLV already provides typed frames (heartbeat) and 4096-byte read buffering; no benefit for this change.
- **Unified `TaskEnvelope` message** instead of `Task`/`TaskResult` pair: rejected — the two directions have genuinely different shapes and the TLV `type_id` already distinguishes direction.

### 5. Gateway: type extraction from URL path

In `GatewayHttpHandler::HandleMessage(HttpRequest, Connection&)`:

1. If `path` does not start with `/tasks/` → HTTP 404 Not Found.
2. Strip the `/tasks/` prefix; strip any query string (split at `?` — the bundled llhttp has no `llhttp_parse_url` helper). Remaining non-empty value is the `type`.
3. Empty type (e.g. `/tasks/`) → HTTP 400 Bad Request.
4. Build `Task`: `id` = existing monotonic counter, `type` = extracted value, `body` = request body. Serialize with `SerializeToString()`, wrap in a `kTaskSubmission` TLV frame, write to the node's connection.

`SerializationFailure` (unlikely for a well-formed message) is treated as a fatal error for the request (log + close / 500).

### 6. Nodeagent: parse and echo

`NodeagentTlvHandler::HandleFrame` for `kTaskSubmission`:
1. Parse the value with `ParseFromArray(frame.value.data(), frame.value.size())` (no copy for parsing).
2. On parse failure → `LOG_WARNING` and drop (per scope decision).
3. Build `TaskResult { id = task.id(), body = task.body() }`, serialize, wrap in a `kResult` frame, write back on the same connection.

The hardcoded echo behavior is preserved verbatim; only the representation changes. Dispatch by `type` is explicitly deferred.

### 7. Gateway: parse results

`GatewayTlvHandler::HandleFrame` for `kResult`:
1. Parse the value as `TaskResult` via `ParseFromArray`.
2. On parse failure → `LOG_WARNING` and drop.
3. Look up `storage_.get(result.id())`, `Deliver(result.body())`, `erase(result.id())`.

This replaces the manual `memcpy` of the first 8 bytes.

### 8. Type extraction location

Type parsing lives in `GatewayHttpHandler`, not in `LlhttpParser` — the parser stays a generic HTTP parser, while `/tasks/{type}` is gateway business logic. This keeps `LlhttpParser` reusable for `GET /tasks` later.

## Risks / Trade-offs

- **No endianness/alignment portability concerns removed** — the old format used native byte order; protobuf is fully portable. The gateway and nodeagent must be upgraded together (breaking wire change) → single-deployment constraint documented in the proposal; rollback means reverting both binaries.
- **Protobuf copy overhead** — serialization copies into the frame buffer and parsing runs over a span; bodies now pass through protoc-generated code instead of a raw memcpy → negligible for current traffic; the zero-copy fast path for the body is preserved at the parser layer (span into chunk memory).
- **Path accumulation** — URLs are typically a single llhttp callback, so the `std::string path_` is small; fragmentation handling adds one string append per fragment. No realistic size concern.
- **Malformed protobuf is dropped silently (logged)** — with only echo semantics there is no error channel; if a handler registry later needs error propagation, a `TaskResult.error` field (or a new TLV type) can be added non-breaking.
- **Field-number freeze** — `id=1`, `type=2`, `body=3` are wired into the protocol; once live, they must not be repurposed (renaming fields is fine).

## Migration Plan

- Single deployment: gateway and nodeagent binaries ship together; no cross-version compatibility required.
- No runtime config changes: the URL shape (`POST /tasks/{type}`) and TLV types are unchanged, so `gateway.yaml` / `nodeagent.yaml` are unaffected.
- Rollback: revert both binaries to the previous revision (wire format reverts with the code).

## Open Questions

- None blocking. (Handler extension interface, supported-type advertisement, and `GET /tasks` are tracked as future proposals.)
