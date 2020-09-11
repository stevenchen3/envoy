#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "absl/container/flat_hash_map.h"
#include "udpa/core/v1/context_params.pb.h"

namespace Envoy {
namespace Config {

// Utilities for working with context parameters.
class UdpaContextParams {
public:
  /**
   * Encode context parameters by following the xDS transport precedence algorithm and applying
   * parameter prefixes.
   * @param node reference to the local Node information.
   * @param node_context_params a list of node fields to include in context parameters.
   * @param resource_context_params context parameters from resource locator.
   * @param client_features client feature capabilities.
   * @param extra_resource_param per-resource type well known attributes.
   * @return udpa::core::v1::ContextParams encoded context parameters.
   */
  static udpa::core::v1::ContextParams
  encode(const envoy::config::core::v3::Node& node,
         const std::vector<std::string>& node_context_params,
         const udpa::core::v1::ContextParams& resource_context_params,
         const std::vector<std::string>& client_features,
         const absl::flat_hash_map<std::string, std::string>& extra_resource_params);
};

} // namespace Config
} // namespace Envoy
