#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/config/extensions.pb.h"
#include "common/task/task.pb.h"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent::task_handlers::openworkflow::test {

// Captures child submissions so tests can complete them either synchronously
// (inside Submit, exercising the reentrancy guard) or later.
class FakeChildTaskSubmitter final : public nodeagent::ChildTaskSubmitter {
public:
  struct Submission {
    std::string id_;
    task::Task task_;
    gateway::ResultReceiverPtr receiver_;
  };

  using OnSubmit = std::move_only_function<void(const task::Task&, gateway::ResultReceiver&)>;

  void Submit(task::Task task, gateway::ResultReceiverPtr receiver) override {
    const std::string id = task.id();
    if (on_submit_) {
      on_submit_(task, *receiver);
    }
    submissions_.push_back(Submission{id, std::move(task), std::move(receiver)});
  }

  auto last() -> Submission* { return submissions_.empty() ? nullptr : &submissions_.back(); }
  auto at(std::size_t index) -> Submission* {
    return index >= submissions_.size() ? nullptr : &submissions_[index];
  }
  auto receiver() -> gateway::ResultReceiver* {
    Submission* submission = last();
    return submission == nullptr ? nullptr : submission->receiver_.get();
  }
  auto receiver(std::size_t index) -> gateway::ResultReceiver* {
    Submission* submission = at(index);
    return submission == nullptr ? nullptr : submission->receiver_.get();
  }
  auto size() const -> std::size_t { return submissions_.size(); }
  void Clear() { submissions_.clear(); }

  void SetOnSubmit(OnSubmit on_submit) { on_submit_ = std::move(on_submit); }

private:
  OnSubmit on_submit_;
  std::vector<Submission> submissions_;
};

// A ResultSender capturing sent results and the close callback, mirroring how
// the connection's mailbox owns that callback.
class TestSender final : public nodeagent::ResultSender {
public:
  struct State {
    std::vector<task::TaskResult> sent_;
    std::move_only_function<void()> close_cb_;
    std::size_t next_token_ = 0;
  };

  explicit TestSender(std::shared_ptr<State> state) : state_{std::move(state)} {}

  void Send(task::TaskResult result) override { state_->sent_.push_back(std::move(result)); }
  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override {
    state_->close_cb_ = std::move(close_cb);
    return ++state_->next_token_;
  }
  void UnregisterOnClose(std::size_t /*token*/) override { state_->close_cb_ = nullptr; }

private:
  std::shared_ptr<State> state_;
};

// Delivers a successful chunk to the most recent submission.
inline void CompleteSuccess(FakeChildTaskSubmitter& submitter, std::string_view body,
                            bool is_final = true) {
  auto* receiver = submitter.receiver();
  const auto data = std::as_bytes(std::span(body.data(), body.size()));
  receiver->Deliver(data, is_final);
}

inline void CompleteSuccessAt(FakeChildTaskSubmitter& submitter, std::size_t index,
                              std::string_view body, bool is_final = true) {
  auto* receiver = submitter.receiver(index);
  const auto data = std::as_bytes(std::span(body.data(), body.size()));
  receiver->Deliver(data, is_final);
}

inline void CompleteError(FakeChildTaskSubmitter& submitter, std::string_view reason,
                          task::TaskStatus status) {
  submitter.receiver()->DeliverError(reason, status);
}

inline void CompleteErrorAt(FakeChildTaskSubmitter& submitter, std::size_t index,
                            std::string_view reason, task::TaskStatus status) {
  submitter.receiver(index)->DeliverError(reason, status);
}

// The bundled jq evaluator selection used by the tests.
inline auto JqEvaluatorConfig() -> config::ExtensionConfig {
  config::ExtensionConfig config;
  config.set_name("jq");
  return config;
}

} // namespace strij::nodeagent::task_handlers::openworkflow::test
