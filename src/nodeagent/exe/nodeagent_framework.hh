#pragma once

namespace strij::nodeagent {

// Runs the node agent: parses CLI flags, loads config, wires the framework
// (admission control, task handlers, local schedulers, TLV listener) and blocks
// on the event dispatcher until shutdown. Extension factories are resolved from
// the registry, so linked alwayslink extension libraries participate without
// code changes. Returns the process exit code.
auto RunNodeagent(int argc, char** argv) -> int;

} // namespace strij::nodeagent