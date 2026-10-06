#include "common/core/utils/task_status.hh"

namespace strij::utils {

namespace {

constexpr int kHttpStatusOk = 200;
constexpr int kHttpStatusBadRequest = 400;
constexpr int kHttpStatusNotFound = 404;
constexpr int kHttpStatusTooManyRequests = 429;
constexpr int kHttpStatusInternalServerError = 500;
constexpr int kHttpStatusNotImplemented = 501;
constexpr int kHttpStatusServiceUnavailable = 503;
constexpr int kHttpStatusGatewayTimeout = 504;

} // namespace

auto IsTaskResultOk(const task::TaskResult& result) -> bool {
  return !result.has_status() || result.status() == task::TASK_STATUS_OK;
}

auto TaskStatusToHttpStatus(task::TaskStatus status) -> int {
  switch (status) {
  case task::TASK_STATUS_OK:
    return kHttpStatusOk;
  case task::TASK_STATUS_MALFORMED_REQUEST:
    return kHttpStatusBadRequest;
  case task::TASK_STATUS_NOT_FOUND:
    return kHttpStatusNotFound;
  case task::TASK_STATUS_UNIMPLEMENTED:
    return kHttpStatusNotImplemented;
  case task::TASK_STATUS_DEADLINE_EXCEEDED:
    return kHttpStatusGatewayTimeout;
  case task::TASK_STATUS_CAPACITY_REFUSED:
    return kHttpStatusTooManyRequests;
  case task::TASK_STATUS_INTERNAL:
    return kHttpStatusInternalServerError;
  case task::TASK_STATUS_UNAVAILABLE:
    return kHttpStatusServiceUnavailable;
  default:
    // proto3 enums are open: a value this schema does not name reads as a
    // generic internal failure, never as success.
    return kHttpStatusInternalServerError;
  }
}

auto HttpStatusPhrase(int http_status) -> std::string_view {
  switch (http_status) {
  case kHttpStatusOk:
    return "OK";
  case kHttpStatusBadRequest:
    return "Bad Request";
  case kHttpStatusNotFound:
    return "Not Found";
  case kHttpStatusTooManyRequests:
    return "Too Many Requests";
  case kHttpStatusNotImplemented:
    return "Not Implemented";
  case kHttpStatusServiceUnavailable:
    return "Service Unavailable";
  case kHttpStatusGatewayTimeout:
    return "Gateway Timeout";
  default:
    return "Internal Server Error";
  }
}

} // namespace strij::utils
