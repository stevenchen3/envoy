#include "test/server/config_validation/xds_fuzz.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

namespace Envoy {

// helper functions to build API responses
envoy::config::cluster::v3::Cluster XdsFuzzTest::buildCluster(const std::string& name) {
  return ConfigHelper::buildCluster(name, "ROUND_ROBIN", api_version_);
};

envoy::config::endpoint::v3::ClusterLoadAssignment
XdsFuzzTest::buildClusterLoadAssignment(const std::string& name) {
  return ConfigHelper::buildClusterLoadAssignment(
      name, Network::Test::getLoopbackAddressString(ip_version_),
      fake_upstreams_[0]->localAddress()->ip()->port(), api_version_);
}

envoy::config::listener::v3::Listener XdsFuzzTest::buildListener(std::string listener_name,
                                                                 std::string route_name) {
  return ConfigHelper::buildListener(listener_name, route_name,
                                     Network::Test::getLoopbackAddressString(ip_version_),
                                     "ads_test", api_version_);
}

envoy::config::route::v3::RouteConfiguration XdsFuzzTest::buildRouteConfig(std::string route_name) {
  return ConfigHelper::buildRouteConfig(route_name, "cluster_0", api_version_);
}

// helper functions to send API responses
void XdsFuzzTest::updateListener(
    const std::vector<envoy::config::listener::v3::Listener>& listeners,
    const std::vector<envoy::config::listener::v3::Listener>& added_or_updated,
    const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(info, "Sending Listener DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener,
                                                               listeners, added_or_updated, removed,
                                                               std::to_string(version_));
}

void XdsFuzzTest::updateRoute(
    const std::vector<envoy::config::route::v3::RouteConfiguration> routes,
    const std::vector<envoy::config::route::v3::RouteConfiguration>& added_or_updated,
    const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(info, "Sending Route DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, routes, added_or_updated, removed,
      std::to_string(version_));
}

XdsFuzzTest::XdsFuzzTest(const test::server::config_validation::XdsTestCase& input,
                         envoy::config::core::v3::ApiVersion api_version)
    : HttpIntegrationTest(
          Http::CodecClient::Type::HTTP2, TestEnvironment::getIpVersionsForTest()[0],
          ConfigHelper::adsBootstrap(input.config().sotw_or_delta() ==
                                             test::server::config_validation::Config::SOTW
                                         ? "GRPC"
                                         : "DELTA_GRPC",
                                     api_version)),
      actions_(input.actions()), version_(1), api_version_(api_version),
      ip_version_(TestEnvironment::getIpVersionsForTest()[0]), num_added_(0), num_modified_(0),
      num_removed_(0) {
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = false;

  if (input.config().sotw_or_delta() == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = Grpc::SotwOrDelta::Sotw;
  } else {
    sotw_or_delta_ = Grpc::SotwOrDelta::Delta;
  }
}

/**
 * initialize an envoy configured with a fully dynamic bootstrap with ADS over gRPC
 */
void XdsFuzzTest::initialize() {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    auto* grpc_service = ads_config->add_grpc_services();

    std::string cluster_name = "ads_cluster";
    grpc_service->mutable_envoy_grpc()->set_cluster_name(cluster_name);
    auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
    ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ads_cluster->set_name("ads_cluster");
  });
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  HttpIntegrationTest::initialize();
  if (xds_stream_ == nullptr) {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }
}

void XdsFuzzTest::close() {
  cleanUpXdsConnection();
  test_server_.reset();
  fake_upstreams_.clear();
}

/**
 * remove a listener from the list of listeners if it exists
 * @param the listener number to be removed
 * @return the listener as an optional so that it can be used in a delta request
 */
bool XdsFuzzTest::eraseListener(std::string listener_name) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->name() == listener_name) {
      listeners_.erase(it);
      return true;
    }
  }
  return false;
}

/**
 * remove a route from the list of routes if it exists
 * @param the route number to be removed
 * @return the route as an optional so that it can be used in a delta request
 */
bool XdsFuzzTest::eraseRoute(std::string route_name) {
  for (auto it = routes_.begin(); it != routes_.end(); ++it) {
    if (it->name() == route_name) {
      routes_.erase(it);
      return true;
    }
  }
  return false;
}

/**
 * send an xDS response to add a listener and update state accordingly
 */
void XdsFuzzTest::addListener(std::string listener_name, std::string route_name) {
  ENVOY_LOG_MISC(info, "Adding {} with reference to {}", listener_name, route_name);
  bool removed = eraseListener(listener_name);
  auto listener = buildListener(listener_name, route_name);
  listeners_.push_back(listener);

  updateListener(listeners_, {listener}, {});
  // use waitForAck instead of compareDiscoveryRequest as the client makes
  // additional discoveryRequests at launch that we might not want to
  // respond to yet
  EXPECT_TRUE(waitForAck(Config::TypeUrl::get().Listener, std::to_string(version_)));
  if (removed) {
    num_modified_++;
    test_server_->waitForCounterGe("listener_manager.listener_modified", num_modified_);
  } else {
    num_added_++;
    test_server_->waitForCounterGe("listener_manager.listener_added", num_added_);
  }
}

/**
 * send an xDS response to remove a listener and update state accordingly
 */
void XdsFuzzTest::removeListener(std::string listener_name) {
  ENVOY_LOG_MISC(info, "Removing {}", listener_name);
  bool removed = eraseListener(listener_name);

  if (removed) {
    num_removed_++;
    updateListener(listeners_, {}, {listener_name});
    EXPECT_TRUE(waitForAck(Config::TypeUrl::get().Listener, std::to_string(version_)));
    test_server_->waitForCounterGe("listener_manager.listener_removed", num_removed_);
  }
}

/**
 * send an xDS response to add a route and update state accordingly
 */
void XdsFuzzTest::addRoute(std::string route_name) {
  ENVOY_LOG_MISC(info, "Adding {}", route_name);
  bool removed = eraseRoute(route_name);
  auto route = buildRouteConfig(route_name);
  routes_.push_back(route);

  if (removed) {
    // if the route was already in routes_, don't send a duplicate add in delta request
    updateRoute(routes_, {}, {});
  } else {
    updateRoute(routes_, {route}, {});
  }

  EXPECT_TRUE(waitForAck(Config::TypeUrl::get().RouteConfiguration, std::to_string(version_)));
}

/**
 * this is a no-op for now because it seems like routes cannot be removed - leaving a route out of
 * a SOTW request does not remove it and sending a remove message in a delta request is ignored
 */
void XdsFuzzTest::removeRoute(std::string route_name) {
  ENVOY_LOG_MISC(info, "Ignoring request to remove {}", route_name);
  return;

  // TODO(samflattery): remove if it's true that routes cannot be removed
  auto removed = eraseRoute(route_name);
  if (removed) {
    updateRoute(routes_, {}, {route_name});
    EXPECT_TRUE(waitForAck(Config::TypeUrl::get().RouteConfiguration, std::to_string(version_)));
  }
}

/**
 * wait for a specific ACK, ignoring any other ACKs that are made in the meantime
 * @param the expected API type url of the ack
 * @param the expected version number
 * @return AssertionSuccess() if the ack was received, else an AssertionError()
 */
AssertionResult XdsFuzzTest::waitForAck(const std::string& expected_type_url,
                                        const std::string& expected_version) {
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw) {
    API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
    do {
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
      ENVOY_LOG_MISC(info, "Received gRPC message with type {} and version {}",
                     discovery_request.type_url(), discovery_request.version_info());
    } while (expected_type_url != discovery_request.type_url() ||
             expected_version != discovery_request.version_info());
  } else {
    API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_discovery_request;
    do {
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
      ENVOY_LOG_MISC(info, "Received gRPC message with type {}",
                     delta_discovery_request.type_url());
    } while (expected_type_url != delta_discovery_request.type_url());
  }
  version_++;
  return AssertionSuccess();
}

/**
 * run the sequence of actions defined in the fuzzed protobuf
 */
void XdsFuzzTest::replay() {
  initialize();

  // set up cluster
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "0");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "0");

  // the client will not subscribe to the RouteConfiguration type URL until it
  // receives a listener, and the ACKS it sends back seem to be an empty type
  // URL so just don't check them until a listener is added
  bool sent_listener = false;

  for (const auto& action : actions_) {
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kAddListener: {
      std::string listener_name =
          absl::StrCat("listener_", action.add_listener().listener_num() % ListenersMax);
      std::string route_name =
          absl::StrCat("route_config_", action.add_listener().route_num() % RoutesMax);
      addListener(listener_name, route_name);
      if (!sent_listener) {
        addRoute(route_name);
        test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
      }
      sent_listener = true;
      break;
    }
    case test::server::config_validation::Action::kRemoveListener: {
      std::string listener_name =
          absl::StrCat("listener_", action.remove_listener().listener_num() % ListenersMax);
      removeListener(listener_name);
      break;
    }
    case test::server::config_validation::Action::kAddRoute: {
      if (!sent_listener) {
        ENVOY_LOG_MISC(info, "Ignoring request to add route_{}",
                       action.add_route().route_num() % RoutesMax);
        break;
      }
      std::string route_name =
          absl::StrCat("route_config_", action.add_route().route_num() % RoutesMax);
      addRoute(route_name);
      break;
    }
    case test::server::config_validation::Action::kRemoveRoute: {
      std::string route_name =
          absl::StrCat("route_config_", action.remove_route().route_num() % RoutesMax);
      removeRoute(route_name);
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  close();
}

} // namespace Envoy
