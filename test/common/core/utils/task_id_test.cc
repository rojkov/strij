#include <regex>
#include <string>

#include "common/core/utils/task_id.hh"
#include "gtest/gtest.h"

namespace strij::utils {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(GenerateTaskIdTest, FormatIsValid) {
  auto task_id = GenerateTaskId();
  std::regex pattern(R"(^[a-z]+_[a-z]+_[a-z]+_[a-z0-9]{8}$)");
  EXPECT_TRUE(std::regex_match(task_id, pattern))
      << "ID does not match expected format: " << task_id;
}

TEST(GenerateTaskIdTest, UsesThreeWordsAndSuffix) {
  auto task_id = GenerateTaskId();
  // Count underscores: there should be exactly 3
  int underscore_count = 0;
  for (char chr : task_id) {
    if (chr == '_') {
      ++underscore_count;
    }
  }

  EXPECT_EQ(underscore_count, 3);
}

TEST(GenerateTaskIdTest, SuffixIsLowercaseAlphanumeric) {
  auto task_id = GenerateTaskId();
  auto suffix_part = task_id.substr(task_id.size() - 8);

  for (char chr : suffix_part) {
    EXPECT_TRUE((chr >= 'a' && chr <= 'z') || (chr >= '0' && chr <= '9'))
        << "Suffix character '" << chr << "' is not lowercase alphanumeric";
  }
}

TEST(GenerateTaskIdTest, ProducesDifferentIds) {
  auto id1 = GenerateTaskId();
  auto id2 = GenerateTaskId();
  EXPECT_NE(id1, id2);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::utils