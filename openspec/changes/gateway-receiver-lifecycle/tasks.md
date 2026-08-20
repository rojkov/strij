## 1. ResultReceiverStorage: node-aware cleanup

- [ ] 1.1 Add `node_of_task_` map (`std::unordered_map<std::string, std::string>`) and modify `put()` to accept and store `node_id`
- [ ] 1.2 Modify `erase()` to also clear the `node_of_task_` entry
- [ ] 1.3 Add `NotifyNodeDisconnected(const std::string& node_id)` method: iterate `node_of_task_`, find matching tasks, call `receiver->DeliverError()`, `erase()`, and `tracker->RecordCompletion()` for each

## 2. HTTP client drop cleanup

- [ ] 2.1 In `GatewayHttpHandler::HandleMessage`, after `storage_.put()`, register a mailbox close callback on the HTTP connection that calls `storage_.erase(task_id)` and `state_tracker_->RecordCompletion(task_id)`

## 3. Node connection drop cleanup

- [ ] 3.1 Add `ResultReceiverStorage&` and `ExactStateTracker*` fields to `Node`, accepted via constructor
- [ ] 3.2 In `Node::HandleCompletion(kConnect)`, after creating the connection, register a mailbox close callback that calls `storage_.NotifyNodeDisconnected(node_id_)`

## 4. Wiring

- [ ] 4.1 Update `NodeDirectory` to accept and forward `ResultReceiverStorage&` and `ExactStateTracker*` to `Node` construction
- [ ] 4.2 Update `gateway.cc` to pass `storage` and `state_tracker` through `NodeDirectory` to `Node`

## 5. Tests

- [ ] 5.1 Test: HTTP client drop — register close callback, fire it, verify receiver erased and tracker recorded completion
- [ ] 5.2 Test: Node drop — call `NotifyNodeDisconnected`, verify all receivers for that node erased, errors delivered, tracker recorded completions
- [ ] 5.3 Test: Idempotent double-erase — HTTP callback fires after receiver already erased by final result, verify no crash
- [ ] 5.4 Test: Node drop with no receivers — `NotifyNodeDisconnected` for a node with no tasks is a no-op
- [ ] 5.5 Test: Node drop delivers error to still-connected HTTP client — verify `DeliverError` called when HTTP connection alive
