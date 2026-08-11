# node-discovery

## Purpose

Allow the gateway to discover node identities and addresses from pluggable sources (static config, etcd, Consul, mDNS, DNS SRV) and to reconcile runtime membership from repeated discovery snapshots instead of a one-shot static list.

## MODIFIED Requirements

### Requirement: NodeDiscovery interface

The system SHALL provide a `NodeDiscovery` interface with `start(DiscoveryCallback)` and `stop()` methods. `DiscoveryCallback` SHALL be `std::function<void(std::vector<NodeInfo>)>`, and `NodeInfo` SHALL contain a `node_id` string and an `address` string. A discovery source that does not know stable node identities SHALL derive `node_id` from the address. The callback SHALL be invocable multiple times; each invocation SHALL deliver the complete current node set (a full snapshot), and the gateway SHALL reconcile it against current membership.

#### Scenario: NodeDiscovery stop

- **WHEN** `stop()` is called on a started `StaticNodeDiscovery` with addresses configured
- **THEN** no error occurs (no-op)

#### Scenario: NodeInfo carries node_id and address

- **WHEN** a `NodeInfo` is produced with `node_id = "n1"` and `address = "10.0.0.1:9090"`
- **THEN** the gateway SHALL read both values

#### Scenario: Callback delivers a full snapshot on each invocation

- **WHEN** the discovery callback is invoked with a node set
- **THEN** the set SHALL represent the complete current node set, not a delta

## ADDED Requirements

### Requirement: Static discovery derives node_id from address

`StaticNodeDiscovery` SHALL produce one `NodeInfo` per configured address with `node_id` derived from the address and the canonical `node_id` learned later from the node's advertisement.

#### Scenario: Static NodeInfo identity is address-derived

- **WHEN** a `StaticNodeDiscovery` with address `"10.0.0.1:9090"` invokes its callback
- **THEN** the delivered `NodeInfo` SHALL have `node_id == "10.0.0.1:9090"` and `address == "10.0.0.1:9090"`
