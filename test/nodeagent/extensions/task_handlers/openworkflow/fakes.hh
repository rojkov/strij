#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/config/extensions.pb.h"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "common/task/task.pb.h"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/event_bus.hh"
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

// Captures publishes and lets tests drive subscriptions synchronously.
class FakeEventBus final : public nodeagent::EventBus {
public:
  class FakeSubscription final : public nodeagent::EventSubscription {
  public:
    explicit FakeSubscription(FakeEventBus& bus, std::string subscription_id)
        : bus_{bus}, subscription_id_{std::move(subscription_id)} {}

    void Ack(std::string_view token) override {
      acked_.emplace_back(token);
      bus_.acked_tokens_.emplace_back(token);
    }
    void Detach() override { detached_ = true; }
    void Delete() override {
      deleted_ = true;
      bus_.subscribers_.erase(subscription_id_);
    }

    std::vector<std::string> acked_;
    bool detached_{false};
    bool deleted_{false};
    FakeEventBus& bus_;
    std::string subscription_id_;
  };

  struct Subscriber {
    event::EventSubscriptionRequest request_;
    FakeSubscription* subscription_{nullptr};
    ReadyCallback on_ready_;
    EventCallback on_event_;
    ErrorCallback on_error_;
  };

  auto Publish(event::CloudEvent event) -> absl::Status override {
    if (!publish_status_.ok()) {
      return publish_status_;
    }
    published_.push_back(std::move(event));
    return absl::OkStatus();
  }

  auto Subscribe(event::EventSubscriptionRequest request, ReadyCallback on_ready,
                 EventCallback on_event, ErrorCallback on_error) -> EventSubscriptionPtr override {
    const std::string id = request.subscription_id();
    auto subscription = std::make_unique<FakeSubscription>(*this, id);
    subscribers_[id] = std::make_shared<Subscriber>(Subscriber{.request_ = std::move(request),
                                                               .subscription_ = subscription.get(),
                                                               .on_ready_ = std::move(on_ready),
                                                               .on_event_ = std::move(on_event),
                                                               .on_error_ = std::move(on_error)});
    if (defer_ready_) {
      return subscription;
    }

    auto subscriber = subscribers_.at(id);
    const absl::Status ready_status = ready_status_.ok() ? absl::OkStatus() : ready_status_;
    std::move(subscriber->on_ready_)(ready_status, delivery_mode_);
    return subscription;
  }

  void Deliver(std::string_view subscription_id, const event::CloudEvent& event,
               std::string_view token) {
    auto iter = subscribers_.find(std::string(subscription_id));
    if (iter == subscribers_.end()) {
      return;
    }
    std::shared_ptr<Subscriber> subscriber = iter->second;
    if (subscriber->on_event_) {
      subscriber->on_event_(event, token);
    }
  }

  void DeliverError(std::string_view subscription_id, absl::Status status) {
    auto iter = subscribers_.find(std::string(subscription_id));
    if (iter == subscribers_.end()) {
      return;
    }
    std::shared_ptr<Subscriber> subscriber = iter->second;
    if (subscriber->on_error_) {
      subscriber->on_error_(std::move(status));
    }
  }

  void SetPublishStatus(absl::Status status) { publish_status_ = std::move(status); }
  void SetReadyStatus(absl::Status status) { ready_status_ = std::move(status); }
  void SetDeliveryMode(event::DeliveryMode mode) { delivery_mode_ = mode; }
  void SetDeferReady(bool defer_ready) { defer_ready_ = defer_ready; }

  void ResolveSubscription(std::string_view subscription_id, absl::Status status,
                           event::DeliveryMode mode) {
    auto iter = subscribers_.find(std::string(subscription_id));
    if (iter == subscribers_.end()) {
      return;
    }
    std::shared_ptr<Subscriber> subscriber = iter->second;
    if (subscriber->on_ready_) {
      std::move(subscriber->on_ready_)(std::move(status), mode);
    }
  }

  std::vector<event::CloudEvent> published_;
  std::vector<std::string> acked_tokens_;
  std::unordered_map<std::string, std::shared_ptr<Subscriber>> subscribers_;

private:
  absl::Status publish_status_;
  absl::Status ready_status_;
  event::DeliveryMode delivery_mode_{event::DELIVERY_MODE_BEST_EFFORT};
  bool defer_ready_{false};
};

// The bundled jq evaluator selection used by the tests.
inline auto JqEvaluatorConfig() -> config::ExtensionConfig {
  config::ExtensionConfig config;
  config.set_name("jq");
  return config;
}

} // namespace strij::nodeagent::task_handlers::openworkflow::test
