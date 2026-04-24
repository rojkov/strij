#pragma once

#include "core/logging/logger.hh"

#define LOG_REGISTER_THREAD() carrot::logging::Logger::GetInstance().RegisterThread()

#define LOG(msg)                                                                                   \
  do {                                                                                             \
    if (carrot::logging::Logger::local_context_ != nullptr) {                                      \
      carrot::logging::Logger::local_context_->Log(msg);                                           \
    }                                                                                              \
  } while (0)