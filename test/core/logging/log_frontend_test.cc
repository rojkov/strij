#include "test/mocks/event/mocks.hh"

#include "core/logging/log_frontend.hh"
#include "gtest/gtest.h"

namespace carrot::logging {
namespace {

using ::testing::_;

TEST(LogFrontendTest, simple_log) {
  auto mocked_dispatcher = std::make_shared<event::MockDispatcher>();
  EXPECT_CALL(*mocked_dispatcher, PrepareRead(_, _, _, _));

  LogFrontend lf(mocked_dispatcher);
  LogEntry entry{};

  lf.Log(std::move(entry));
}

template <typename... Args> inline void pass_test_args(std::byte*& ptr, Args&&... args) {
  (pack_arg(ptr, args), ...);
}

TEST(LogFrontendTest, pack_args_trivially_copyable) {
  LogEntry entry{};
  std::byte* ptr = entry.args_data_;
  uint32_t first_arg{12345};
  uint64_t second_arg{6789};
  uint32_t third_arg{10};
  uint32_t fifth_arg{77777};
  pass_test_args(ptr, first_arg, second_arg, third_arg, "test", fifth_arg);
  ASSERT_EQ(*(uint32_t*)entry.args_data_, first_arg);
  ASSERT_EQ(*(uint64_t*)(entry.args_data_ + sizeof(first_arg)), second_arg);
  ASSERT_EQ(*(uint32_t*)(entry.args_data_ + sizeof(first_arg) + sizeof(second_arg)), third_arg);
  ASSERT_EQ(std::string{(char*)(entry.args_data_ + sizeof(first_arg) + sizeof(second_arg) +
                                sizeof(third_arg))},
            "test");
  ASSERT_EQ(*(uint32_t*)(entry.args_data_ + sizeof(first_arg) + sizeof(second_arg) +
                         sizeof(third_arg) + sizeof("test")),
            fifth_arg);
}

TEST(LogFrontendTest, pack_args_string_view) {
  LogEntry entry{};
  std::byte* ptr = entry.args_data_;
  auto fn = [&ptr](std::string_view str) { pass_test_args(ptr, str, uint32_t(10)); };
  std::string test_str{"test"};
  fn(test_str);

  ASSERT_EQ(*(size_t*)entry.args_data_, test_str.size());
  char str_data[512]{};
  std::memcpy(&str_data, entry.args_data_ + sizeof(size_t), test_str.size());
  ASSERT_EQ(std::string_view{(char*)str_data}, test_str);
  ASSERT_EQ(*(uint32_t*)(entry.args_data_ + sizeof(size_t) + test_str.size()), uint32_t(10));
}

TEST(LogFrontendTest, pack_args_string) {
  LogEntry entry{};
  std::byte* ptr = entry.args_data_;
  std::string test_str{"test"};
  pass_test_args(ptr, test_str, uint32_t(10));

  ASSERT_EQ(*(size_t*)entry.args_data_, test_str.size());
  char str_data[512]{};
  std::memcpy(&str_data, entry.args_data_ + sizeof(size_t), test_str.size());
  ASSERT_EQ(std::string_view{(char*)str_data}, test_str);
  ASSERT_EQ(*(uint32_t*)(entry.args_data_ + sizeof(size_t) + test_str.size()), uint32_t(10));
}

template <typename... Args>
inline void init_log_entry(LogEntry& entry, const char* fmt_str, Args&&... args) {
  entry.fmt_str_ = fmt_str;
  entry.format_fn_ = get_format_fn<Args...>();
  std::byte* ptr = entry.args_data_;
  (pack_arg(ptr, args), ...);
}

TEST(LogFrontendTest, unpack) {
  LogEntry entry{};
  {
    std::string test_str{"test_str"};
    std::string_view test_str_view{"test_str_view"};
    init_log_entry(entry, "{} | {} : {}", 10, test_str, test_str_view);
  }

  std::string output;
  entry.format_fn_(entry.fmt_str_, entry.args_data_, output);
  ASSERT_EQ(output, "10 | test_str : test_str_view");
}

} // namespace
} // namespace carrot::logging
