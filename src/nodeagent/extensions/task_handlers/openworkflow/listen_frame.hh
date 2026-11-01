#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
  void SetSubscription(EventSubscriptionPtr subscription);
  void SetReady(absl::Status status, event::DeliveryMode mode);

private:
  struct QueuedEvent {
    event::CloudEvent event_;
    std::string token_;
  };

  [[nodiscard]] auto start(FrameContext& context) -> std::optional<FrameResult>;
  [[nodiscard]] auto process(const QueuedEvent& queued, FrameContext& context)
      -> std::optional<FrameResult>;
  [[nodiscard]] auto completeWith(std::vector<event::CloudEvent> events) -> FrameResult;
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
  std::vector<std::optional<event::CloudEvent>> all_matches_;
  std::map<std::string, bool> seen_;
  std::optional<ExecutionFailure> failure_;
  bool complete_{false};
  StepDataValue result_;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
