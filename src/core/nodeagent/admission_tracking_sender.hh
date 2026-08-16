#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "core/nodeagent/admission_controller.hh"
#include "extensions/task_handlers/task_handlers.hh"

namespace strij::nodeagent {

/**
 * @brief ResultSender that releases its admission scope on final result.
 *
 * Wraps a concrete ResultSender (the ConnectionResultSender bound to the
 * connection's mailbox) and releases the AdmissionScope held for the task
 * when the final result is sent. If the handler is destroyed without ever
 * sending a final result (a dropped task), the scope releases in its
 * destructor instead. Release() is idempotent, so both paths may run.
 */
class AdmissionTrackingSender final : public extensions::ResultSender {
public:
  AdmissionTrackingSender(extensions::ResultSenderPtr inner, AdmissionScopePtr scope)
      : inner_{std::move(inner)}, scope_{std::move(scope)} {}

  ~AdmissionTrackingSender() override = default;

  AdmissionTrackingSender(const AdmissionTrackingSender&) = delete;
  auto operator=(const AdmissionTrackingSender&) -> AdmissionTrackingSender& = delete;
  AdmissionTrackingSender(AdmissionTrackingSender&&) noexcept = delete;
  auto operator=(AdmissionTrackingSender&&) noexcept -> AdmissionTrackingSender& = delete;

  void Send(task::TaskResult result) override {
    const bool is_final = !result.has_is_final() || result.is_final();
    inner_->Send(std::move(result));

    if (is_final) {
      scope_->Release();
    }
  }

  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override {
    return inner_->RegisterOnClose(std::move(close_cb));
  }

  void UnregisterOnClose(std::size_t token) override { inner_->UnregisterOnClose(token); }

private:
  extensions::ResultSenderPtr inner_;
  AdmissionScopePtr scope_;
};

} // namespace strij::nodeagent
