#include "nodeagent/extensions/task_handlers/openworkflow/listen_frame.hh"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "common/core/logging/log.hh"
#include "common/core/utils/jv.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "openworkflow/events.hh"
#include "openworkflow/task.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

namespace {
constexpr std::size_t kMaxPendingEvents = 1024;
} // namespace

using ::strij::openworkflow::EventConsumptionStrategy;
using ::strij::openworkflow::ListenBody;
using ::strij::openworkflow::NamedTask;

namespace {

auto jsonField(std::string_view name, std::string_view value) -> std::string {
  return absl::StrCat("\"", name, "\":\"", absl::CEscape(value), "\"");
}

auto jsonValueOrString(std::string_view value) -> std::string {
  auto parsed = utils::Jv::Parse(value);
  if (parsed.ok()) {
    return std::string(value);
  }
  return absl::StrCat("\"", absl::CEscape(value), "\"");
}

// Renders an opaque CloudEvent `data` payload as a JSON value. An absent
// payload becomes `null`; a JSON payload is preserved; any other payload is a
// JSON string so the listen output (and a foreach selected value) stays valid
// JSON. Filters and `until` already treat absent data as null.
auto eventDataJson(std::string_view data) -> std::string {
  if (data.empty()) {
    return "null";
  }
  return jsonValueOrString(data);
}

} // namespace

ListenFrame::ListenFrame(const NamedTask& task, const ListenBody& body,
                         std::shared_ptr<const LexicalScope> scope)
    : task_{task}, body_{body}, scope_{std::move(scope)} {
  if (body_.foreach_.has_value()) {
    auto names = NormalizeLoopVariables(body_.foreach_->item_, body_.foreach_->at_);
    if (names.ok()) {
      foreach_item_name_ = names->each_;
      foreach_index_name_ = names->at_;
    }
  }
}

void ListenFrame::Enter(StepDataValue data) {
  input_ = std::move(data);
  all_matches_.resize(body_.to_.filters_.size());
  markActive();
}

void ListenFrame::SetSubscription(EventSubscriptionPtr subscription) {
  subscription_ = std::move(subscription);
}

void ListenFrame::SetReady(absl::Status status, event::DeliveryMode mode) {
  ready_ = true;
  ready_status_ = std::move(status);
  mode_ = mode;
}

void ListenFrame::OnEvent(event::CloudEvent event, std::string token) {
  if (queued_.size() >= kMaxPendingEvents) {
    LOG_WARNING("Dropping event for listen '{}': pending queue is full", task_.name_);
    return;
  }
  queued_.push_back(QueuedEvent{.event_ = std::move(event), .token_ = std::move(token)});
}

void ListenFrame::OnSubscriptionError(absl::Status status) {
  if (IsTerminal() || !IsActive()) {
    return;
  }

  failure_ = ExecutionFailure{.status_ = task::TASK_STATUS_UNAVAILABLE,
                              .origin_ = nullptr,
                              .step_ = task_.name_,
                              .message_ = std::string(status.message()),
                              .composed_ = false};
  markFailed();
}

namespace {

auto isReservedEnvelopeField(std::string_view name) -> bool {
  for (std::string_view reserved : {"specversion", "id", "source", "type", "subject", "time",
                                    "datacontenttype", "dataschema", "data"}) {
    if (name == reserved) {
      return true;
    }
  }
  return false;
}

} // namespace

auto ListenFrame::itemBytes(const event::CloudEvent& event) const -> std::string {
  const bool envelope = body_.read_.has_value() && *body_.read_ == "envelope";
  if (!envelope) {
    return eventDataJson(event.data());
  }

  std::string json = "{";
  bool first = true;
  const auto field = [&json, &first](std::string_view name, std::string_view value) {
    if (!first) {
      json.push_back(',');
    }
    first = false;
    absl::StrAppend(&json, jsonField(name, value));
  };

  field("specversion", event.specversion());
  field("id", event.id());
  field("source", event.source());
  field("type", event.type());
  if (!event.subject().empty()) {
    field("subject", event.subject());
  }
  if (!event.time().empty()) {
    field("time", event.time());
  }
  if (!event.datacontenttype().empty()) {
    field("datacontenttype", event.datacontenttype());
  }
  if (!event.dataschema().empty()) {
    field("dataschema", event.dataschema());
  }
  for (const auto& [name, value] : event.attributes()) {
    if (!isReservedEnvelopeField(name)) {
      field(name, value);
    }
  }

  if (!first) {
    json.push_back(',');
  }
  absl::StrAppend(&json, "\"data\":", eventDataJson(event.data()), "}");
  return json;
}

auto ListenFrame::rememberEvent(const std::string& key) -> bool {
  if (!seen_.insert(key).second) {
    return false;
  }

  seen_order_.push_back(key);
  while (seen_order_.size() > kMaxTrackedEventIds) {
    seen_.erase(seen_order_.front());
    seen_order_.pop_front();
  }

  return true;
}

auto ListenFrame::completeWith(std::vector<event::CloudEvent> events) -> FrameResult {
  std::string json = "[";
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index > 0) {
      json.push_back(',');
    }
    json.append(itemBytes(events[index]));
  }
  json.push_back(']');

  complete_ = true;
  result_ = StepDataValue::FromBytes(std::move(json));
  markCompleted();
  return Completed{.value_ = result_.Copy()};
}

auto ListenFrame::completeForeach() -> FrameResult {
  std::string json = "[";
  for (std::size_t index = 0; index < foreach_results_.size(); ++index) {
    if (index > 0) {
      json.push_back(',');
    }
    json.append(jsonValueOrString(foreach_results_[index]));
  }
  json.push_back(']');

  complete_ = true;
  result_ = StepDataValue::FromBytes(std::move(json));
  markCompleted();
  return Completed{.value_ = result_.Copy()};
}

auto ListenFrame::startForeachBody(FrameContext& /*context*/) -> std::optional<FrameResult> {
  if (foreach_pending_.empty()) {
    return std::nullopt;
  }

  event::CloudEvent event = std::move(foreach_pending_.front().event_);
  const std::string token = std::move(foreach_pending_.front().token_);
  foreach_pending_.pop_front();
  StepDataValue selected = StepDataValue::FromBytes(itemBytes(event));

  auto index = utils::Jv::Parse(std::to_string(foreach_index_++));
  if (!index.ok()) {
    return fail(task::TASK_STATUS_INTERNAL, "failed to build the foreach index");
  }

  utils::Jv item = selected.IsJson() ? selected.Json().Copy() : [&]() -> utils::Jv {
    auto parsed = utils::Jv::Parse(selected.Bytes());
    if (parsed.ok()) {
      return std::move(*parsed);
    }
    auto null_value = utils::Jv::Parse("null");
    return null_value.ok() ? std::move(*null_value) : utils::Jv{};
  }();

  foreach_scope_ = std::make_shared<LexicalScope>(LexicalScope{
      .parent_ = scope_,
      .each_name_ = foreach_item_name_,
      .item_ = std::move(item),
      .at_name_ = foreach_index_name_,
      .index_ = std::move(*index),
  });
  foreach_body_ = std::make_unique<SequenceFrame>(
      body_.foreach_->do_, std::nullopt, kDefaultSynchronousTransitionBudget, foreach_scope_);
  foreach_selected_ = std::move(selected);
  foreach_token_ = token;
  foreach_body_->Enter(foreach_selected_.Copy());
  return std::nullopt;
}

void ListenFrame::ackForeachEvent() {
  if (subscription_ != nullptr && foreach_token_.has_value()) {
    subscription_->Ack(*foreach_token_);
  }
  foreach_token_.reset();
}

auto ListenFrame::fail(task::TaskStatus status, std::string message) -> FrameResult {
  failure_ = ExecutionFailure{.status_ = status,
                              .origin_ = nullptr,
                              .step_ = task_.name_,
                              .message_ = std::move(message),
                              .composed_ = false};
  markFailed();
  return Failed{.failure_ = *failure_};
}

namespace {

auto listenStatusFromError(const absl::Status& status) -> task::TaskStatus {
  switch (status.code()) {
  case absl::StatusCode::kInvalidArgument:
    return task::TASK_STATUS_MALFORMED_REQUEST;
  case absl::StatusCode::kUnimplemented:
    return task::TASK_STATUS_UNIMPLEMENTED;
  case absl::StatusCode::kUnavailable:
    return task::TASK_STATUS_UNAVAILABLE;
  default:
    return task::TASK_STATUS_INTERNAL;
  }
}

} // namespace

auto ListenFrame::start(FrameContext& context) -> std::optional<FrameResult> {
  const absl::Status status = context.StartListen(*this, task_, body_, input_, scope_.get());
  if (!status.ok()) {
    return fail(listenStatusFromError(status), std::string(status.message()));
  }

  started_ = true;
  return std::nullopt;
}

auto ListenFrame::process(const QueuedEvent& queued, FrameContext& context)
    -> std::optional<FrameResult> {
  const auto ack = [this, &queued]() {
    if (subscription_ != nullptr) {
      subscription_->Ack(queued.token_);
    }
  };

  const std::string key = queued.event_.source() + ":" + queued.event_.id();
  if (!rememberEvent(key)) {
    ack();
    return std::nullopt;
  }

  // Once the consumption strategy has completed, later deliveries must not
  // feed another foreach iteration (notably `one`, which stops at the first
  // matching event). Acknowledge them so a durable plane can advance.
  if (strategy_complete_) {
    ack();
    return std::nullopt;
  }

  bool matched = false;
  for (std::size_t index = 0; index < body_.to_.filters_.size(); ++index) {
    auto match =
        context.EvaluateListenFilter(task_, body_.to_.filters_[index], queued.event_, scope_.get());
    if (!match.ok()) {
      ack();
      return fail(task::TASK_STATUS_INTERNAL, std::string(match.status().message()));
    }

    if (!*match) {
      continue;
    }

    matched = true;
    if (body_.to_.mode_ == EventConsumptionStrategy::Mode::kAll) {
      all_matches_[index] = queued;
    }
  }

  if (body_.foreach_.has_value()) {
    switch (body_.to_.mode_) {
    case EventConsumptionStrategy::Mode::kOne:
      if (matched) {
        foreach_pending_.push_back(queued);
        strategy_complete_ = true;
      } else {
        ack();
      }
      break;
    case EventConsumptionStrategy::Mode::kAll: {
      bool all = true;
      for (const auto& slot : all_matches_) {
        if (!slot.has_value()) {
          all = false;
          break;
        }
      }
      if (!matched) {
        ack();
      }
      if (all) {
        strategy_complete_ = true;
        foreach_pending_.clear();
        for (const auto& slot : all_matches_) {
          foreach_pending_.push_back(*slot);
        }
      }
      break;
    }
    case EventConsumptionStrategy::Mode::kAny:
      if (matched) {
        consumed_.push_back(queued.event_);
        foreach_pending_.push_back(queued);
        if (!body_.to_.until_.has_value()) {
          strategy_complete_ = true;
        } else {
          auto until = context.EvaluateListenUntil(task_, body_, consumed_, scope_.get());
          if (!until.ok()) {
            ack();
            return fail(task::TASK_STATUS_INTERNAL, std::string(until.status().message()));
          }
          if (*until) {
            strategy_complete_ = true;
          }
        }
      } else if (body_.to_.until_.has_value() && !body_.to_.until_->IsString()) {
        std::vector<event::CloudEvent> candidate = consumed_;
        candidate.push_back(queued.event_);
        auto until = context.EvaluateListenUntil(task_, body_, candidate, scope_.get());
        if (!until.ok()) {
          ack();
          return fail(task::TASK_STATUS_INTERNAL, std::string(until.status().message()));
        }
        if (*until) {
          consumed_ = std::move(candidate);
          foreach_pending_.push_back(queued);
          strategy_complete_ = true;
        } else {
          ack();
        }
      } else {
        ack();
      }
      break;
    }
    return std::nullopt;
  }

  std::optional<FrameResult> result;
  switch (body_.to_.mode_) {
  case EventConsumptionStrategy::Mode::kOne:
    if (matched) {
      result = completeWith({queued.event_});
    }
    break;
  case EventConsumptionStrategy::Mode::kAll: {
    bool all = true;
    std::vector<event::CloudEvent> events;
    for (const auto& slot : all_matches_) {
      if (!slot.has_value()) {
        all = false;
        break;
      }
      events.push_back(slot->event_);
    }
    if (all) {
      result = completeWith(std::move(events));
    }
    break;
  }
  case EventConsumptionStrategy::Mode::kAny:
    if (matched) {
      consumed_.push_back(queued.event_);
      if (!body_.to_.until_.has_value()) {
        result = completeWith({queued.event_});
      } else {
        auto until = context.EvaluateListenUntil(task_, body_, consumed_, scope_.get());
        if (!until.ok()) {
          ack();
          return fail(task::TASK_STATUS_INTERNAL, std::string(until.status().message()));
        }
        if (*until) {
          result = completeWith(consumed_);
        }
      }
    } else if (body_.to_.until_.has_value() && !body_.to_.until_->IsString()) {
      std::vector<event::CloudEvent> candidate = consumed_;
      candidate.push_back(queued.event_);
      auto until = context.EvaluateListenUntil(task_, body_, candidate, scope_.get());
      if (!until.ok()) {
        ack();
        return fail(task::TASK_STATUS_INTERNAL, std::string(until.status().message()));
      }
      if (*until) {
        result = completeWith(std::move(candidate));
      }
    }
    break;
  }

  ack();
  return result;
}

auto ListenFrame::Advance(FrameContext& context) -> FrameResult {
  if (IsAbandoned()) {
    return Suspended{};
  }

  if (failure_.has_value()) {
    if (subscription_ != nullptr) {
      subscription_->Delete();
      subscription_.reset();
    }
    if (IsActive()) {
      markFailed();
    }
    return Failed{.failure_ = *failure_};
  }

  if (complete_) {
    return Completed{.value_ = result_.Copy()};
  }

  assert(IsActive());

  if (!started_) {
    if (auto failure = start(context); failure.has_value()) {
      return std::move(*failure);
    }
  }

  if (!ready_) {
    return Suspended{};
  }

  if (!ready_status_.ok()) {
    if (subscription_ != nullptr) {
      subscription_->Delete();
      subscription_.reset();
    }
    return fail(listenStatusFromError(ready_status_), std::string(ready_status_.message()));
  }

  while (!queued_.empty()) {
    QueuedEvent queued = std::move(queued_.front());
    queued_.pop_front();
    if (auto result = process(queued, context); result.has_value()) {
      if (subscription_ != nullptr) {
        subscription_->Delete();
        subscription_.reset();
      }
      return std::move(*result);
    }
  }

  if (body_.foreach_.has_value()) {
    while (true) {
      if (foreach_body_ != nullptr) {
        FrameResult result = foreach_body_->Advance(context);
        if (std::holds_alternative<Suspended>(result)) {
          return Suspended{};
        }
        if (auto* ended = std::get_if<Ended>(&result); ended != nullptr) {
          ackForeachEvent();
          if (subscription_ != nullptr) {
            subscription_->Delete();
            subscription_.reset();
          }
          return Ended{.value_ = std::move(ended->value_)};
        }
        if (auto* failed = std::get_if<Failed>(&result); failed != nullptr) {
          ackForeachEvent();
          if (subscription_ != nullptr) {
            subscription_->Delete();
            subscription_.reset();
          }
          failure_ = std::move(failed->failure_);
          markFailed();
          return Failed{.failure_ = *failure_};
        }

        StepDataValue body_output = std::move(std::get<Completed>(result).value_);
        auto transformed = context.TransformListenForeachOutput(*body_.foreach_, foreach_selected_,
                                                                body_output, *foreach_scope_);
        if (!transformed.ok()) {
          ackForeachEvent();
          foreach_body_.reset();
          foreach_scope_.reset();
          if (subscription_ != nullptr) {
            subscription_->Delete();
            subscription_.reset();
          }
          return fail(task::TASK_STATUS_INTERNAL, std::string(transformed.status().message()));
        }

        const absl::Status exported = context.ExportListenForeach(
            *body_.foreach_, foreach_selected_, *transformed, *foreach_scope_);
        if (!exported.ok()) {
          ackForeachEvent();
          foreach_body_.reset();
          foreach_scope_.reset();
          if (subscription_ != nullptr) {
            subscription_->Delete();
            subscription_.reset();
          }
          return fail(task::TASK_STATUS_INTERNAL, std::string(exported.message()));
        }

        ackForeachEvent();
        foreach_results_.push_back(transformed->Bytes());
        foreach_body_.reset();
        foreach_scope_.reset();
        continue;
      }

      if (!foreach_pending_.empty()) {
        if (auto failure = startForeachBody(context); failure.has_value()) {
          return std::move(*failure);
        }
        continue;
      }

      if (strategy_complete_) {
        if (subscription_ != nullptr) {
          subscription_->Delete();
          subscription_.reset();
        }
        return completeForeach();
      }

      return Suspended{};
    }
  }

  return Suspended{};
}

void ListenFrame::Abandon(FrameContext& context) {
  if (IsTerminal()) {
    return;
  }

  if (subscription_ != nullptr) {
    subscription_->Delete();
    subscription_.reset();
  }

  if (foreach_body_ != nullptr) {
    foreach_body_->Abandon(context);
    foreach_body_.reset();
  }

  foreach_token_.reset();
  markAbandoned();
}

} // namespace strij::nodeagent::task_handlers::openworkflow
