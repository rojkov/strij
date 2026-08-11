#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/gateway/node_directory.hh"
#include "core/io/protocol_parser.hh"
#include "core/node/capabilities.pb.h"
#include "extensions/node_discovery/node_discovery.hh"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

using strij::extensions::NodeInfo;
using ::testing::_;
using ::testing::DoAll;
using ::testing::SaveArg;

auto Snapshot(std::initializer_list<NodeInfo> infos) -> std::vector<NodeInfo> {
  return infos;
}

class NodeDirectoryTest : public ::testing::Test {
protected:
  std::shared_ptr<strij::event::MockDispatcher> dispatcher_{
      std::make_shared<strij::event::MockDispatcher>()};

  // A ConnectionFactory that reports parser destruction through a shared flag,
  // observable as the Connection being torn down.
  auto MakeTrackingFactory(std::shared_ptr<bool> parser_destroyed) -> strij::io::ConnectionFactory {
    return [parser_destroyed](strij::io::Connection&)
               -> std::unique_ptr<strij::io::ProtocolParser> {
      class TrackingParser final : public strij::io::ProtocolParser {
      public:
        explicit TrackingParser(std::shared_ptr<bool> destroyed) : destroyed_{std::move(destroyed)} {}
        ~TrackingParser() override { *destroyed_ = true; }
        auto GetReadBuffer() -> std::span<std::byte> override {
          return std::span<std::byte>(buf_.data(), buf_.size());
        }
        auto OnData(size_t /*bytes_read*/) -> strij::io::ProtocolParser::Action override {
          return strij::io::ProtocolParser::Action::NeedMoreData;
        }

      private:
        std::shared_ptr<bool> destroyed_;
        std::array<std::byte, 128> buf_{};
      };
      return std::make_unique<TrackingParser>(parser_destroyed);
    };
  }

  auto MakeTrivialFactory() -> strij::io::ConnectionFactory {
    return [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
      return std::make_unique<strij::io::TrivialParser>();
    };
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(NodeDirectoryTest, StartsEmpty) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());
  EXPECT_EQ(directory.GetNodeCount(), 0U);
  EXPECT_EQ(directory.GetAvailableCount(), 0U);
}

TEST_F(NodeDirectoryTest, AddNodeStartsConnecting) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  strij::event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher_,
              PrepareConnect(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&node_completable), ::testing::Return()));

  directory.AddNode("n2", "10.0.0.3:9090");

  EXPECT_EQ(directory.GetNodeCount(), 1U);
  ASSERT_NE(directory.GetNode("n2"), nullptr);
  EXPECT_EQ(directory.GetNode("n2")->GetStatus(), strij::gateway::Node::Status::kConnecting);
  EXPECT_NE(node_completable, nullptr);
}

TEST_F(NodeDirectoryTest, RemoveNodeDropsAndClosesConnection) {
  auto parser_destroyed = std::make_shared<bool>(false);
  NodeDirectory directory(dispatcher_, MakeTrackingFactory(parser_destroyed));

  strij::event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&node_completable), ::testing::Return()));
  directory.AddNode("n1", "10.0.0.1:9090");

  node_completable->HandleCompletion(0, 0, 0);
  ASSERT_FALSE(*parser_destroyed);

  directory.RemoveNode("n1");

  EXPECT_EQ(directory.GetNodeCount(), 0U);
  EXPECT_EQ(directory.GetNode("n1"), nullptr);
  EXPECT_TRUE(*parser_destroyed);
}

TEST_F(NodeDirectoryTest, RemoveUnknownNodeIsNoop) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());
  directory.RemoveNode("10.0.0.6:9090");
  EXPECT_EQ(directory.GetNodeCount(), 0U);
}

TEST_F(NodeDirectoryTest, ReconcileAddsAndRemoves) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(3);
  directory.Reconcile(Snapshot({NodeInfo{"A", "10.0.0.1:9090"}, NodeInfo{"B", "10.0.0.2:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 2U);

  directory.Reconcile(Snapshot({NodeInfo{"A", "10.0.0.1:9090"}, NodeInfo{"C", "10.0.0.3:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 2U);
  EXPECT_NE(directory.GetNode("A"), nullptr);
  EXPECT_EQ(directory.GetNode("B"), nullptr);
  EXPECT_NE(directory.GetNode("C"), nullptr);
}

TEST_F(NodeDirectoryTest, ReconcileUnchangedSnapshotIsNoop) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(2);
  directory.Reconcile(Snapshot({NodeInfo{"A", "10.0.0.1:9090"}, NodeInfo{"B", "10.0.0.2:9090"}}));

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(0);
  directory.Reconcile(Snapshot({NodeInfo{"A", "10.0.0.1:9090"}, NodeInfo{"B", "10.0.0.2:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 2U);
}

TEST_F(NodeDirectoryTest, ReconcileReconnectsWhenAddressChanges) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(2);
  directory.Reconcile(Snapshot({NodeInfo{"n1", "10.0.0.1:9090"}}));
  auto* node = directory.GetNode("n1");
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->GetAddress(), "10.0.0.1:9090");

  directory.Reconcile(Snapshot({NodeInfo{"n1", "10.0.0.2:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 1U);
  EXPECT_NE(directory.GetNode("n1"), nullptr);
  EXPECT_EQ(directory.GetNode("n1")->GetAddress(), "10.0.0.2:9090");
}

TEST_F(NodeDirectoryTest, ReconcileMultipleSnapshotsDriveMembership) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(3);
  directory.Reconcile(Snapshot({NodeInfo{"A", "10.0.0.1:9090"}, NodeInfo{"B", "10.0.0.2:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 2U);

  directory.Reconcile(Snapshot({NodeInfo{"B", "10.0.0.2:9090"}, NodeInfo{"C", "10.0.0.3:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 2U);
  EXPECT_EQ(directory.GetNode("A"), nullptr);
  EXPECT_NE(directory.GetNode("B"), nullptr);
  EXPECT_NE(directory.GetNode("C"), nullptr);
}

TEST_F(NodeDirectoryTest, RekeyNodeMovesRecordToCanonicalId) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(1);
  directory.AddNode("10.0.0.4:9090", "10.0.0.4:9090");
  directory.RekeyNode("10.0.0.4:9090", "n1");

  EXPECT_EQ(directory.GetNode("10.0.0.4:9090"), nullptr);
  ASSERT_NE(directory.GetNode("n1"), nullptr);
  EXPECT_EQ(directory.GetNode("n1")->GetNodeId(), "n1");
  EXPECT_EQ(directory.GetNodeCount(), 1U);
}

TEST_F(NodeDirectoryTest, RekeyCollisionKeepsOriginalRecord) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).Times(2);
  directory.AddNode("10.0.0.4:9090", "10.0.0.4:9090");
  directory.AddNode("n1", "10.0.0.5:9090");

  directory.RekeyNode("10.0.0.4:9090", "n1");

  EXPECT_NE(directory.GetNode("10.0.0.4:9090"), nullptr);
  EXPECT_NE(directory.GetNode("n1"), nullptr);
  EXPECT_EQ(directory.GetNodeCount(), 2U);
}

TEST_F(NodeDirectoryTest, GetNextNodeReturnsNullWhenNoneAvailable) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).WillRepeatedly(::testing::Return());
  directory.AddNode("n1", "10.0.0.1:9090");

  EXPECT_EQ(directory.GetNextNode(), nullptr);
}

TEST_F(NodeDirectoryTest, GetNextNodeReturnsConnectedNode) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  strij::event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&node_completable), ::testing::Return()));
  directory.AddNode("n1", "10.0.0.1:9090");
  node_completable->HandleCompletion(0, 0, 0);

  EXPECT_EQ(directory.GetNextNode(), directory.GetNode("n1"));
  EXPECT_EQ(directory.GetAvailableCount(), 1U);
}

TEST_F(NodeDirectoryTest, ReconcileResolvesRekeyedPlaceholderIdentity) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).WillRepeatedly(::testing::Return());
  directory.AddNode("10.0.0.1:9090", "10.0.0.1:9090");
  directory.RekeyNode("10.0.0.1:9090", "n1");

  // A subsequent snapshot still reporting the placeholder identity resolves to
  // the canonical record instead of adding a duplicate.
  directory.Reconcile(Snapshot({NodeInfo{"10.0.0.1:9090", "10.0.0.1:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 1U);
  EXPECT_EQ(directory.GetNode("10.0.0.1:9090"), nullptr);
  EXPECT_NE(directory.GetNode("n1"), nullptr);

  // Removing the canonical node also forgets the origin mapping: a fresh
  // snapshot re-adds a clean record.
  directory.RemoveNode("n1");
  EXPECT_EQ(directory.GetNodeCount(), 0U);
  directory.Reconcile(Snapshot({NodeInfo{"10.0.0.1:9090", "10.0.0.1:9090"}}));
  EXPECT_EQ(directory.GetNodeCount(), 1U);
  EXPECT_NE(directory.GetNode("10.0.0.1:9090"), nullptr);
}

namespace {

auto MakeCapabilities(std::initializer_list<std::string> protocols)
    -> strij::node::NodeCapabilities {
  strij::node::NodeCapabilities caps;
  for (const auto& protocol : protocols) {
    caps.add_scheduling_protocols()->set_name(protocol);
  }
  return caps;
}

} // namespace

TEST_F(NodeDirectoryTest, GetCandidatesFiltersByAdvertisingProtocol) {
  NodeDirectory directory(dispatcher_, MakeTrivialFactory());

  EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).WillRepeatedly(::testing::Return());
  directory.AddNode("push", "10.0.0.1:9090");
  directory.AddNode("probe", "10.0.0.2:9090");
  directory.AddNode("quiet", "10.0.0.3:9090");

  // Bring all three nodes up.
  directory.GetNode("push")->HandleCompletion(0, 0, 0);
  directory.GetNode("probe")->HandleCompletion(0, 0, 0);
  directory.GetNode("quiet")->HandleCompletion(0, 0, 0);
  // "quiet" stays connected but never advertises.

  directory.GetNode("push")->StoreCapabilities(MakeCapabilities({"push"}));
  directory.GetNode("probe")->StoreCapabilities(MakeCapabilities({"probe"}));

  auto push_candidates = directory.GetCandidates("push");
  ASSERT_EQ(push_candidates.size(), 2U);
  EXPECT_EQ(push_candidates[0], directory.GetNode("push"));
  // "quiet" is connected without an advertisement and treated as eligible;
  // "probe" advertises a different protocol and is excluded.
  EXPECT_EQ(push_candidates[1], directory.GetNode("quiet"));

  auto probe_candidates = directory.GetCandidates("probe");
  ASSERT_EQ(probe_candidates.size(), 2U);
  EXPECT_EQ(probe_candidates[0], directory.GetNode("probe"));
  EXPECT_EQ(probe_candidates[1], directory.GetNode("quiet"));
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway
