1.17.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* config: v2 is now fatal-by-default. This may be overridden by setting :option:`--bootstrap-version` 2 on the CLI for a v2 bootstrap file and also enabling the runtime `envoy.reloadable_features.enable_deprecated_v2_api` feature.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* build: the Alpine based debug images are no longer built in CI, use Ubuntu based images instead.
* cluster manager: the cluster which can't extract secret entity by SDS to be warming and never activate. This feature is disabled by default and is controlled by runtime guard `envoy.reloadable_features.cluster_keep_warming_no_secret_entity`.
* expr filter: added `connection.termination_details` property support.
* ext_authz filter: disable `envoy.reloadable_features.ext_authz_measure_timeout_on_check_created` by default.
* ext_authz filter: the deprecated field :ref:`use_alpha <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.use_alpha>` is no longer supported and cannot be set anymore.
* grpc_web filter: if a `grpc-accept-encoding` header is present it's passed as-is to the upstream and if it isn't `grpc-accept-encoding:identity` is sent instead. The header was always overwriten with `grpc-accept-encoding:identity,deflate,gzip` before.
* memory: enable new tcmalloc with restartable sequences for aarch64 builds.
* tls: removed RSA key transport and SHA-1 cipher suites from the client-side defaults.
* watchdog: the watchdog action :ref:`abort_action <envoy_v3_api_msg_watchdog.v3alpha.AbortActionConfig>` is now the default action to terminate the process if watchdog kill / multikill is enabled.
* xds: to support TTLs, heartbeating has been added to xDS. As a result, responses that contain empty resources without updating the version will no longer be propagated to the
  subscribers. To undo this for VHDS (which is the only subscriber that wants empty resources), the `envoy.reloadable_features.vhds_heartbeats` can be set to "false".

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* config: validate that upgrade configs have a non-empty :ref:`upgrade_type <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.upgrade_type>`, fixing a bug where an errant "-" could result in unexpected behavior.
* dns: fix a bug where custom resolvers provided in configuration were not preserved after network issues.
* dns_filter: correctly associate DNS response IDs when multiple queries are received.
* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* http: reject requests with missing required headers after filter chain processing.
* http: sending CONNECT_ERROR for HTTP/2 where appropriate during CONNECT requests.
* proxy_proto: fixed a bug where the wrong downstream address got sent to upstream connections.
* tls: fix detection of the upstream connection close event.
* tls: fix read resumption after triggering buffer high-watermark and all remaining request/response bytes are stored in the SSL connection's internal buffers.
* watchdog: touch the watchdog before most event loop operations to avoid misses when handling bursts of callbacks.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* ext_authz: removed auto ignore case in HTTP-based `ext_authz` header matching and the runtime guard `envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher`. To ignore case, set the :ref:`ignore_case <envoy_api_field_type.matcher.StringMatcher.ignore_case>` field to true.
* http: flip default HTTP/1 and HTTP/2 server codec implementations to new codecs that remove the use of exceptions for control flow. To revert to old codec behavior, set the runtime feature `envoy.reloadable_features.new_codec_behavior` to false.
* http: removed `envoy.reloadable_features.http1_flood_protection` and legacy code path for turning flood protection off.

New Features
------------
* config: added new runtime feature `envoy.features.enable_all_deprecated_features` that allows the use of all deprecated features.
* grpc: implemented header value syntax support when defining :ref:`initial metadata <envoy_v3_api_field_config.core.v3.GrpcService.initial_metadata>` for gRPC-based `ext_authz` :ref:`HTTP <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.grpc_service>` and :ref:`network <envoy_v3_api_field_extensions.filters.network.ext_authz.v3.ExtAuthz.grpc_service>` filters, and :ref:`ratelimit <envoy_v3_api_field_config.ratelimit.v3.RateLimitServiceConfig.grpc_service>` filters.
* hds: added support for delta updates in the :ref:`HealthCheckSpecifier <envoy_v3_api_msg_service.health.v3.HealthCheckSpecifier>`, making only the Endpoints and Health Checkers that changed be reconstructed on receiving a new message, rather than the entire HDS.
* health_check: added option to use :ref:`no_traffic_healthy_interval <envoy_v3_api_field_config.core.v3.HealthCheck.no_traffic_healthy_interval>` which allows a different no traffic interval when the host is healthy.
* http: added HCM :ref:`timeout config field <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_headers_timeout>` to control how long a downstream has to finish sending headers before the stream is cancelled.
* http: added frame flood and abuse checks to the upstream HTTP/2 codec. This check is off by default and can be enabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to true.
* jwt_authn: added support for :ref:`per-route config <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.PerRouteConfig>`.
* listener: added an optional :ref:`default filter chain <envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>`. If this field is supplied, and none of the :ref:`filter_chains <envoy_v3_api_field_config.listener.v3.Listener.filter_chains>` matches, this default filter chain is used to serve the connection.
* log: added a new custom flag ``%_`` to the log pattern to print the actual message to log, but with escaped newlines.
* lua: added `downstreamDirectRemoteAddress()` and `downstreamLocalAddress()` APIs to :ref:`streamInfo() <config_http_filters_lua_stream_info_wrapper>`.
* mongo_proxy: the list of commands to produce metrics for is now :ref:`configurable <envoy_v3_api_field_extensions.filters.network.mongo_proxy.v3.MongoProxy.commands>`.
* network: added a :ref:`timeout <envoy_v3_api_field_config.listener.v3.FilterChain.transport_socket_connect_timeout>` for incoming connections completing transport-level negotiation, including TLS and ALTS hanshakes.
* overload: add :ref:`envoy.overload_actions.reduce_timeouts <config_overload_manager_overload_actions>` overload action to enable scaling timeouts down with load.
* ratelimit: added support for use of various :ref:`metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.metadata>` as a ratelimit action.
* ratelimit: added :ref:`disable_x_envoy_ratelimited_header <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>` option to disable `X-Envoy-RateLimited` header.
* tcp: added a new :ref:`envoy.overload_actions.reject_incoming_connections <config_overload_manager_overload_actions>` action to reject incoming TCP connections.
* tls: added support for RSA certificates with 4096-bit keys in FIPS mode.
* tracing: added SkyWalking tracer.
* xds: added support for resource TTLs. A TTL is specified on the :ref:`Resource <envoy_api_msg_Resource>`. For SotW, a :ref:`Resource <envoy_api_msg_Resource>` can be embedded
  in the list of resources to specify the TTL.

Deprecated
----------
* gzip: :ref:`HTTP Gzip filter <config_http_filters_gzip>` is rejected now unless explicitly allowed with :ref:`runtime override <config_runtime_deprecation>` `envoy.deprecated_features.allow_deprecated_gzip_http_filter` set to `true`.
* ratelimit: the :ref:`dynamic metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.dynamic_metadata>` action is deprecated in favor of the more generic :ref:`metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.metadata>` action.
