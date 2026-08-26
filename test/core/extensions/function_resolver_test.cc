#include "core/extensions/function_resolver.hh"
#include "gtest/gtest.h"

namespace strij::extensions {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(LocalFunctionResolverTest, ReturnsReferenceAsPath) {
  LocalFunctionResolver resolver;

  auto result = resolver.Resolve("/usr/bin/cat");

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, "/usr/bin/cat");
}

TEST(LocalFunctionResolverTest, RejectsEmptyReference) {
  LocalFunctionResolver resolver;

  auto result = resolver.Resolve("");

  ASSERT_FALSE(result.ok());
}

TEST(LocalFunctionResolverTest, FunctionParameterConstant) {
  EXPECT_EQ(kFunctionParameter, "function");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions
