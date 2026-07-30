#pragma once

#include <string_view>

#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"

namespace carrot::gateway {

class HttpResultReceiver final : public ResultReceiver {
public:
  explicit HttpResultReceiver(carrot::io::Connection& conn) : conn_{conn} {}

  void Deliver(std::span<const std::byte> value) override;

private:
  carrot::io::Connection& conn_;
};

} // namespace carrot::gateway
