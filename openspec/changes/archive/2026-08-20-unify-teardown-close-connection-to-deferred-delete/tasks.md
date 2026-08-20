# Tasks: Migrate CLOSE_CONNECTION onto DEFERRED_DELETE

- [x] 1. **`Connection::onEndOfStream`**: change `event::Command::CLOSE_CONNECTION` to `event::Command::DEFERRED_DELETE` in `src/core/io/connection.cc:84`
- [x] 2. **`TcpListener::ProcessCommand`**: match on `event::Command::DEFERRED_DELETE` instead of `CLOSE_CONNECTION` in `src/core/io/tcp_listener.cc:69`
- [x] 3. **`Node::ProcessCommand`**: match on `event::Command::DEFERRED_DELETE` instead of `CLOSE_CONNECTION` in `src/core/gateway/node.cc:106`
- [x] 4. **`Command::Type` enum**: remove `CLOSE_CONNECTION` from `include/strij/event/command.hh:12`
- [x] 5. **`Connection` header comment**: update the comment referencing `CLOSE_CONNECTION` in `src/core/io/connection.hh:43`
- [x] 6. **`connection_test.cc`**: change the test assertion from `CLOSE_CONNECTION` to `DEFERRED_DELETE` in `test/core/io/connection_test.cc:237`
- [x] 7. **`connection-completable` spec**: update `openspec/specs/connection-completable/spec.md` — rename `CLOSE_CONNECTION` to `DEFERRED_DELETE` in the end-of-stream scenario (lines 22, 39-43)
- [x] 8. **`node-directory` spec**: update `openspec/specs/node-directory/spec.md` — rename `CLOSE_CONNECTION` to `DEFERRED_DELETE` in the connection-closure scenario (lines 29-32)
- [x] 9. **Build & test**: `make build` and `make test` pass
