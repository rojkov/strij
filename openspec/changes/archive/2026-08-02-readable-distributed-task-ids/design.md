## Context

The gateway currently generates task IDs using a `uint64_t next_task_id_` counter, incremented for each incoming request. This approach assumes a single gateway instance and produces collisions when multiple instances run concurrently. We need to support distributed deployments where dozens to hundreds of gateways may exist.

The existing infrastructure includes:
- `Task` and `TaskResult` protobuf messages in `api/core/task/task.proto` with `uint64 id` field.
- `ResultReceiverStorage` mapping `uint64_t` to receivers.
- No external coordination service (e.g., no ZooKeeper, etcd, or shared database).

Constraints:
- Configuration-free uniqueness (no instance ID allocation).
- Human-readable, pronounceable IDs for log readability.
- C++23 standard library only (no external libraries beyond what exists in the repo).
- Thread-local random state for concurrent gateway threads.

## Goals / Non-Goals

**Goals:**
- Generate task IDs that are virtually collision-free across 1000s of gateway instances.
- IDs should be short enough to read and pronounce naturally (≈20-30 characters).
- Implementation must be configuration-free (no instance ID assignment).
- Use thread-local random state to avoid contention in multi-threaded environments.

**Non-Goals:**
- Do not encode timestamp or instance identity in the ID.
- Do not guarantee lexicographic ordering or sortability by time.
- Do not support human-meaningful patterns (e.g., "task about user 123").
- Do not introduce external dependencies (UUID libraries, snowflake implementations, etc.).

## Decisions

### 1. Generate IDs in format: `adjective_noun_verb_xxxxxxxxxx`

**Decision:** Use three randomly selected English words plus an 8-character alphanumeric suffix: `happy_fox_runs_k7m2x9p4`.

**Rationale:**
- Three words from curated lists provide ~20 million combinations.
- 8-character alphanumeric suffix provides ~2.8 trillion combinations per word triple.
- Total ID space is 56 quintillion, collision probability < 10⁻¹⁰ for 10 million tasks.
- The words make IDs pronounceable and distinguishable in logs.
- The random suffix ensures uniqueness without coordination.

**Alternatives considered:**
- UUID v4 (128 bits): Too long (36 chars), not pronounceable, not human-friendly.
- Snowflake IDs (64 bits): Require timestamp ordering or coordination, not readable.
- Pure random words: Collision risk too high without suffix.
- Instance-prefixed suffixes: Require configuration, contradicting "configuration-free" goal.

---

### 2. Word lists size: 200 adjectives, 500 nouns, 200 verbs

**Decision:** Curate word lists with 200 adjectives, 500 nouns, and 200 verbs.

**Rationale:**
- 200 × 500 × 200 = 20,000,000 word combinations.
- Lists are small enough to review for offensive or ambiguous words.
- Large enough that common words repeat rarely: after 100,000 tasks, ~0.3% of tasks will share a word triple (but suffixes differ).

**Alternatives considered:**
- Docker's noun/adjective lists (300 words × 60 words): Too small for our collision requirements.
- Large word lists (1000+ words each): Harder to vet, more likely to contain awkward combinations.

---

### 3. Use thread-local `std::mt19937_64` PRNG

**Decision:** Each thread calling `GenerateTaskId()` lazily initializes a thread-local `std::mt19937_64` seeded from `std::random_device`.

**Rationale:**
- Thread-local state eliminates lock contention in multi-threaded gateway dispatchers.
- `std::random_device` provides entropy without requiring external seeding configuration.
- `std::mt19937_64` produces high-quality 64-bit random numbers efficiently.

**Alternatives considered:**
- Global mutex-protected PRNG: Creates contention on hot path.
- Lock-free hash table for IDs: More complex than needed, doesn't solve coordination.
- `std::default_random_engine`: Non-portable quality across implementations.

---

### 4. Change protobuf schema to `string id`

**Decision:** Change `Task.id` and `TaskResult.id` from `uint64` to `string`.

**Rationale:**
- String IDs are variable-length, accommodating our word-based format.
- Protobuf string fields are wire-compatible for migration: old uint64 fields and new string fields will serialize differently, but the schema change is explicit and intentional.
- Keeps the task correlation logic simple (use string keys in storage).

**Alternatives considered:**
- Use `bytes id` with binary encoding: Harder to debug, less log-friendly.
- Add a new string field (backward compatibility): More complex schema, no actual compatibility benefit since wire format changes anyway.

---

### 5. Store task IDs as `std::string` in `ResultReceiverStorage`

**Decision:** Change `ResultReceiverStorage` to use `std::unordered_map<std::string, ResultReceiverPtr>`.

**Rationale:**
- String keys naturally match our new ID format.
- `std::unordered_map` has average-case O(1) lookup, suitable for the hot path (task submission and result delivery).

**Alternatives considered:**
- Keep uint64_t storage with hash of string: Adds collision risk, more complex.
- Use sorted map: Slower lookups, unnecessary since we don't need ordered iteration.

---

## Task ID Generation Flow

```
┌─────────────────────────────────────────────────────────────────┐
│ GatewayHttpHandler::HandleMessage(HttpRequest, Connection&)     │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ 1. Call GenerateTaskId()                                 │  │
│  │    └─ returns std::string: "happy_fox_runs_k7m2x9p4"     │  │
│  └──────────────────────────────────────────────────────────┘  │
│         │                                                      │
│         ▼                                                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ 2. Build Task proto: id="happy_fox_runs_k7m2x9p4"        │  │
│  │    type="extracted-type", body=request.body              │  │
│  └──────────────────────────────────────────────────────────┘  │
│         │                                                      │
│         ▼                                                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ 3. Serialize Task to bytes                               │  │
│  │ 4. storage_.put("happy_fox_runs_k7m2x9p4", receiver)     │  │
│  │ 5. SerializeTlvFrame(kTaskSubmission, task_bytes)        │  │
│  │ 6. nodeagent_conn->Write(frame)                          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Task ID Generation Implementation

```
┌─────────────────────────────────────────────────────────────────┐
│ auto GenerateTaskId() -> std::string                           │
│                                                                 │
│ 1. Select random adjective from list (200 words)               │
│ 2. Select random noun from list (500 words)                    │
│ 3. Select random verb from list (200 words)                    │
│ 4. Generate 8 random alphanumeric chars (a-z, 0-9)             │
│ 5. Return std::format("{}_{}_{}_{}", adj, noun, verb, suffix)  │
│                                                                 │
│ Example: "eager_river_flows_x3k2m9p7"                          │
│                                                                 │
│ PRNG: Thread-local std::mt19937_64                             │
│   - Seeded once from std::random_device                        │
│   - No lock contention across threads                          │
└─────────────────────────────────────────────────────────────────┘
```

## Risks / Trade-offs

**Risk: Word lists produce awkward or offensive combinations**
Mitigation: Curate word lists carefully, avoiding:
- Words with negative connotations
- Words from sensitive cultural contexts
- Words that could combine into offensive phrases
- Homophones (words that sound the same when spoken)

**Risk: Protobuf breaking change requires coordinated deployment**
Mitigation: Document this as a breaking change. All gateway and nodeagent instances must be upgraded together or use a rolling deployment with compatible schema versions.

**Risk: Random suffix collisions in high-volume scenarios**
Mitigation: With 8 alphanumeric characters (36⁸ ≈ 2.8 trillion combinations), collision probability is < 10⁻¹⁰ for 10 million tasks. If collisions become a concern, increase suffix length to 10 or 12 characters.

**Risk: ID length impacts log file size and network bandwidth**
Mitigation: 20-30 character IDs are comparable to UUIDs (36 characters). Acceptable tradeoff for readability.

**Risk: Word lists are not internationalized**
Mitigation: Use simple English words that are pronounceable globally. Avoid culture-specific references. Future enhancement could add localization if needed.

---

## Migration Plan

1. **Deploy new nodeagent version** that accepts both `uint64` and `string` task IDs. Old nodeagents will reject the new format, but new nodeagents can handle both during transition.

2. **Deploy new gateway version** that generates string IDs. New gateways will send string IDs, which old nodeagents cannot parse. This step should be done atomically or with all instances upgraded simultaneously.

3. **Monitor for errors** during transition: Log warnings for any `Task` or `TaskResult` messages that fail to parse.

4. **Rollback strategy**: If issues arise, revert to previous gateway/nodeagent versions. Since the old code uses `uint64` IDs, it will not produce string IDs, so the system will fall back to the old behavior.

**Note:** If zero-downtime deployment is critical, introduce a version field in the protocol or support dual-id formats during transition. For simple deployments, coordinate downtime for upgrade.
