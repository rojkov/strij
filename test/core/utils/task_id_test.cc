#include <string>
#include <regex>

#include "core/utils/task_id.hh"
#include "gtest/gtest.h"

namespace carrot::utils {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(GenerateTaskIdTest, FormatIsValid) {
  auto id = GenerateTaskId();
  std::regex pattern(R"(^[a-z]+_[a-z]+_[a-z]+_[a-z0-9]{8}$)");
  EXPECT_TRUE(std::regex_match(id, pattern)) << "ID does not match expected format: " << id;
}

TEST(GenerateTaskIdTest, UsesThreeWordsAndSuffix) {
  auto id = GenerateTaskId();
  // Count underscores: there should be exactly 3
  int underscore_count = 0;
  for (char c : id) {
    if (c == '_') ++underscore_count;
  }
  EXPECT_EQ(underscore_count, 3);
}

TEST(GenerateTaskIdTest, SuffixIsLowercaseAlphanumeric) {
  auto id = GenerateTaskId();
  auto suffix_part = id.substr(id.size() - 8);
  for (char c : suffix_part) {
    EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        << "Suffix character '" << c << "' is not lowercase alphanumeric";
  }
}

TEST(GenerateTaskIdTest, ProducesDifferentIds) {
  auto id1 = GenerateTaskId();
  auto id2 = GenerateTaskId();
  EXPECT_NE(id1, id2);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace carrot::utils