#include "common/conn_pool/conn_pool_base.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace ConnectionPool {

using testing::InvokeWithoutArgs;
using testing::Return;

class TestActiveClient : public ActiveClient {
public:
  using ActiveClient::ActiveClient;
  void close() override { onEvent(Network::ConnectionEvent::LocalClose); }
  uint64_t id() const override { return 1; }
  bool closingWithIncompleteStream() const override { return false; }
  size_t numActiveStreams() const override { return 1; }
};

class TestPendingStream : public PendingStream {
public:
  TestPendingStream(ConnPoolImplBase& parent, AttachContext& context)
      : PendingStream(parent), context_(context) {}
  AttachContext& context() override { return context_; }
  AttachContext& context_;
};

class TestConnPoolImplBase : public ConnPoolImplBase {
public:
  using ConnPoolImplBase::ConnPoolImplBase;
  ConnectionPool::Cancellable* newPendingStream(AttachContext& context) override {
    auto entry = std::make_unique<TestPendingStream>(*this, context);
    LinkedList::moveIntoList(std::move(entry), pending_streams_);
    return pending_streams_.front().get();
  }
  MOCK_METHOD(ActiveClientPtr, instantiateActiveClient, ());
  MOCK_METHOD(void, onPoolFailure,
              (const Upstream::HostDescriptionConstSharedPtr& n, absl::string_view,
               ConnectionPool::PoolFailureReason, AttachContext&));
  MOCK_METHOD(void, onPoolReady, (ActiveClient&, AttachContext&));
};

class ConnPoolImplBaseTest : public testing::Test {
public:
  ConnPoolImplBaseTest()
      : pool_(host_, Upstream::ResourcePriority::Default, dispatcher_, nullptr, nullptr) {
    // Default connections to 1024 because the tests shouldn't be relying on the
    // connection resource limit for most tests.
    cluster_->resetResourceManager(1024, 1024, 1024, 1, 1);
    ON_CALL(pool_, instantiateActiveClient).WillByDefault(Invoke([&]() -> ActiveClientPtr {
      auto ret = std::make_unique<TestActiveClient>(pool_, stream_limit_, concurrent_streams_);
      clients_.push_back(ret.get());
      ret->real_host_description_ = descr_;
      return ret;
    }));
  }

  uint32_t stream_limit_ = 100;
  uint32_t concurrent_streams_ = 1;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> descr_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80")};
  NiceMock<Event::MockDispatcher> dispatcher_;
  TestConnPoolImplBase pool_;
  AttachContext context_;
  std::vector<ActiveClient*> clients_;
};

TEST_F(ConnPoolImplBaseTest, BasicPrefetch) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  // On new stream, create 2 connections.
  EXPECT_CALL(pool_, instantiateActiveClient).Times(2);
  auto cancelable = pool_.newStream(context_);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, PrefetchOnDisconnect) {
  testing::InSequence s;

  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  // On new stream, create 2 connections.
  EXPECT_CALL(pool_, instantiateActiveClient).Times(2);
  pool_.newStream(context_);

  // If a connection fails, existing connections are purged. If a retry causes
  // a new stream, make sure we create the correct number of connections.
  EXPECT_CALL(pool_, onPoolFailure).WillOnce(InvokeWithoutArgs([&]() -> void {
    pool_.newStream(context_);
  }));
  EXPECT_CALL(pool_, instantiateActiveClient).Times(1);
  clients_[0]->close();

  EXPECT_CALL(pool_, onPoolFailure);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, NoPrefetchIfUnhealthy) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  host_->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(host_->health(), Upstream::Host::Health::Unhealthy);

  // On new stream, create 1 connection.
  EXPECT_CALL(pool_, instantiateActiveClient).Times(1);
  auto cancelable = pool_.newStream(context_);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, NoPrefetchIfDegraded) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  EXPECT_EQ(host_->health(), Upstream::Host::Health::Healthy);
  host_->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH);
  EXPECT_EQ(host_->health(), Upstream::Host::Health::Degraded);

  // On new stream, create 1 connection.
  EXPECT_CALL(pool_, instantiateActiveClient).Times(1);
  auto cancelable = pool_.newStream(context_);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_.destructAllConnections();
}

} // namespace ConnectionPool
} // namespace Envoy
