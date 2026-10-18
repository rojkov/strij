#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/artifact_loader.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

class ArtifactLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::uint64_t counter = 0;
    dir_ =
        std::filesystem::temp_directory_path() /
        ("strij-artifact-loader-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }

  auto Path(std::string_view name) const -> std::filesystem::path { return dir_ / name; }

  auto Write(std::string_view name, std::string_view content) -> std::filesystem::path {
    const std::filesystem::path path = Path(name);
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();

    return path;
  }

  std::filesystem::path dir_;
};

TEST_F(ArtifactLoaderTest, LoadsRegularFile) {
  const std::filesystem::path path = Write("workflow.yaml", "document: {}\n");

  auto loaded = LoadWorkflowArtifact(path.string());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  EXPECT_EQ(*loaded, "document: {}\n");
}

TEST_F(ArtifactLoaderTest, LoadsEmptyFile) {
  const std::filesystem::path path = Write("empty.yaml", "");

  auto loaded = LoadWorkflowArtifact(path.string());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  EXPECT_TRUE(loaded->empty());
}

TEST_F(ArtifactLoaderTest, MissingPathIsNotFound) {
  auto loaded = LoadWorkflowArtifact(Path("missing.yaml").string());
  ASSERT_FALSE(loaded.ok());
  EXPECT_EQ(loaded.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(ArtifactLoaderTest, DirectoryIsRejected) {
  const std::filesystem::path path = Path("directory");
  std::filesystem::create_directories(path);

  auto loaded = LoadWorkflowArtifact(path.string());
  ASSERT_FALSE(loaded.ok());
  EXPECT_EQ(loaded.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ArtifactLoaderTest, FifoIsRejectedWithoutBlocking) {
  const std::filesystem::path path = Path("fifo");
  ASSERT_EQ(::mkfifo(path.c_str(), 0600), 0);

  auto loaded = LoadWorkflowArtifact(path.string());
  ASSERT_FALSE(loaded.ok());
  EXPECT_EQ(loaded.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ArtifactLoaderTest, OversizedFileIsRejected) {
  const std::filesystem::path path = Write("large.yaml", "x");
  std::filesystem::resize_file(path, (4 * 1024 * 1024) + 1);

  auto loaded = LoadWorkflowArtifact(path.string());
  ASSERT_FALSE(loaded.ok());
  EXPECT_EQ(loaded.status().code(), absl::StatusCode::kInvalidArgument);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
