#pragma once

namespace strij::gateway {

// Runs the gateway: parses CLI flags, loads config, wires the framework
// (logging, node discovery, scheduler router, HTTP listener) and blocks on the
// event dispatcher until shutdown. Extension factories are resolved from the
// registry, so linked alwayslink extension libraries participate without code
// changes. Returns the process exit code.
auto RunGateway(int argc, char** argv) -> int;

} // namespace strij::gateway