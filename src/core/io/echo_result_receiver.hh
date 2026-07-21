#pragma once

#include <string_view>

#include "core/io/connection.hh"
#include "core/io/result_receiver_storage.hh"

namespace carrot::io {

class EchoResultReceiver final : public ResultReceiver {
public:
  explicit EchoResultReceiver(Connection& conn) : conn_{conn} {}

  void Deliver(std::span<const std::byte> value) override;

private:
  Connection& conn_;
};

} // namespace carrot::io
