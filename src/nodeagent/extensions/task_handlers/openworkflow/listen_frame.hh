#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/frame.hh"
#include "openworkflow/events.hh"
#include "openworkflow/task.hh"
#include "strij/nodeagent/event_bus.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// A resumable `listen` step. It owns one event subscription, applies the
// workflow-level filters and consumption strategy, and completes with the
// consumed events as its raw output.
class ListenFrame final : public Frame {
public:
  // Bounds the event ids retained for duplicate tolerance. A duplicate older
  // than this window may be processed again; v1 delivery is best-effort and
  // at-most-once, so the window only needs to cover near-term redelivery.
  static constexpr std::size_t kMaxTrackedEventIds = 4096;

  ListenFrame(const ::strij::openworkflow::NamedTask& task,
              const ::strij::openworkflow::ListenBody& body,
              std::shared_ptr<const LexicalScope> scope);
  ~ListenFrame() override = default;

  ListenFrame(const ListenFrame&) = delete;
  auto operator=(const ListenFrame&) -> ListenFrame& = delete;
  ListenFrame(ListenFrame&&) noexcept = delete;
  auto operator=(ListenFrame&&) noexcept -> ListenFrame& = delete;

  void Enter(StepDataValue data) override;
  auto Advance(FrameContext& context) -> FrameResult override;
  void Abandon(FrameContext& context) override;

  void OnEvent(event::CloudEvent event, std::string token);
  // An asynchronous subscription failure after it became ready (for example,
  // the gateway connection was lost). Fails the step with UNAVAILABLE.
  void OnSubscriptionError(const absl::Status& status);
  void SetSubscription(EventSubscriptionPtr subscription);
  void SetReady(absl::Status status, event::DeliveryMode mode);

  [[nodiscard]] auto PendingEventCount() const -> std::size_t { return queued_.size(); }
  // Test hook: number of event ids currently retained for duplicate tolerance.
  [[nodiscard]] auto TrackedEventCount() const -> std::size_t { return seen_.size(); }

private:
  struct QueuedEvent {
    event::CloudEvent event_;
    std::string token_;
  };

  [[nodiscard]] auto start(FrameContext& context) -> std::optional<FrameResult>;
  [[nodiscard]] auto process(const QueuedEvent& queued, FrameContext& context)
      -> std::optional<FrameResult>;
  // Records an event id for duplicate tolerance. Returns false when the id is
  // already inside the bounded recent-event window.
  [[nodiscard]] auto rememberEvent(const std::string& key) -> bool;
  [[nodiscard]] auto completeWith(std::vector<event::CloudEvent> events) -> FrameResult;
  [[nodiscard]] auto completeForeach() -> FrameResult;
  [[nodiscard]] auto startForeachBody(FrameContext& context) -> std::optional<FrameResult>;
  void ackForeachEvent();
  [[nodiscard]] auto fail(task::TaskStatus status, std::string message) -> FrameResult;
  [[nodiscard]] auto itemBytes(const event::CloudEvent& event) const -> std::string;

  const ::strij::openworkflow::NamedTask& task_;
  const ::strij::openworkflow::ListenBody& body_;
  std::shared_ptr<const LexicalScope> scope_;
  StepDataValue input_;
  bool started_{false};
  bool ready_{false};
  absl::Status ready_status_;
  event::DeliveryMode mode_{event::DELIVERY_MODE_BEST_EFFORT};
  EventSubscriptionPtr subscription_;
  std::deque<QueuedEvent> queued_;
  std::vector<event::CloudEvent> consumed_;
  std::vector<std::optional<QueuedEvent>> all_matches_;
  std::deque<QueuedEvent> foreach_pending_;
  std::vector<std::string> foreach_results_;
  std::unique_ptr<SequenceFrame> foreach_body_;
  std::shared_ptr<const LexicalScope> foreach_scope_;
  std::string foreach_item_name_{"item"};
  std::string foreach_index_name_{"index"};
  std::size_t foreach_index_{0};
  StepDataValue foreach_selected_;
  std::optional<std::string> foreach_token_;
  bool strategy_complete_{false};
  std::set<std::string> seen_;
  std::deque<std::string> seen_order_;
  std::optional<ExecutionFailure> failure_;
  bool complete_{false};
  StepDataValue result_;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
