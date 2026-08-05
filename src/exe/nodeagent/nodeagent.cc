#include <memory>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "core/common/signal_monitor.hh"
#include "core/config/config_loader.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/extensions/factory_context.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"
#include "core/nodeagent/nodeagent_tlv_handler.hh"
#include "core/nodeagent/task_handler_manager.hh"

// Generated protobuf headers
#include "core/config/nodeagent.pb.h"

// NOLINTBEGIN
ABSL_FLAG(std::string, config_file, "nodeagent.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, port, 0, "Override TLV listener port (0 = use config)");
ABSL_FLAG(std::string, address, "", "Override TLV listener address (empty = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(std::string, log_format, "", "Override log format (text|json)");
// NOLINTEND

auto main(int argc, char** argv) -> int {
  absl::ParseCommandLine(argc, argv);

  // Load configuration
  auto config_result =
      strij::config::LoadConfig<strij::config::NodeAgentConfig>(absl::GetFlag(FLAGS_config_file));

  if (!config_result.ok()) {
    LOG_ERROR("Config error: {}", config_result.status().message());
    return 1;
  }

  strij::config::NodeAgentConfig config = config_result.value();

  const strij::event::DispatcherSharedPtr dispatcher =
      std::make_shared<strij::event::DispatcherImpl>();

  // Build the task handler manager from config. This must run before the
  // --validate_only short-circuit so that unknown handler names fail validation.
  strij::extensions::FactoryContextImpl factory_context(dispatcher);
  auto manager_result =
      strij::nodeagent::BuildTaskHandlerManager(config.task_handlers(), factory_context);
  if (!manager_result.ok()) {
    LOG_ERROR("Task handler config error: {}", manager_result.status().message());
    return 1;
  }
  const std::shared_ptr<strij::nodeagent::TaskHandlerManager>& task_handler_manager =
      manager_result.value();

  if (absl::GetFlag(FLAGS_validate_only)) {
    LOG_INFO("Config validation passed");
    return 0;
  }

  // Apply CLI overrides
  if (absl::GetFlag(FLAGS_port) != 0) {
    config.mutable_tlv_listener()->set_port(absl::GetFlag(FLAGS_port));
  }
  if (!absl::GetFlag(FLAGS_address).empty()) {
    config.mutable_tlv_listener()->set_address(absl::GetFlag(FLAGS_address));
  }
  if (!absl::GetFlag(FLAGS_log_level).empty()) {
    config.mutable_logging()->set_level(absl::GetFlag(FLAGS_log_level));
  }
  if (!absl::GetFlag(FLAGS_log_format).empty()) {
    config.mutable_logging()->set_format(absl::GetFlag(FLAGS_log_format));
  }

  // Signal monitor must be activated before the logger thread, otherwise we might miss the signal.
  const strij::common::SignalMonitor signal_monitor(dispatcher);

  strij::logging::Logger& logger = strij::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  strij::io::TcpListener listener{
      dispatcher, config.tlv_listener().port(),
      [task_handler_manager](
          strij::io::Connection& conn) -> std::unique_ptr<strij::io::ProtocolParser> {
        auto handler =
            std::make_unique<strij::nodeagent::NodeagentTlvHandler>(task_handler_manager);
        return std::make_unique<strij::io::TlvParser>(
            [hdl = std::move(handler), &conn](strij::io::TlvFrame frame) -> void {
              hdl->HandleFrame(frame, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}
