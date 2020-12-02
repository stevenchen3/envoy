.. _config_http_filters_kill_request:

Kill Request
===============

The KillRequest filter can be used to crash Envoy when receiving a Kill request.
By default, KillRequest filter is not built into Envoy binary since it is included in *DISABLED_BY_DEFAULT_EXTENSIONS* in *extensions_build_config.bzl*. If you want to use this extension, please move it from *DISABLED_BY_DEFAULT_EXTENSIONS* to *EXTENSIONS*.

Configuration
-------------

* This filter should be configured with the name *envoy.filters.http.kill_request*.

.. _config_http_filters_kill_request_http_header:

Enable Kill Request via HTTP header
--------------------------------------------

The KillRequest filter requires the following header in the request:

x-envoy-kill-request
  whether the request is a Kill request.
  The header value must be one of (case-insensitive) ["true", "t", "yes", "y", "1"]
  in order for the request to be a Kill request.

.. note::

  If the headers appear multiple times only the first value is used.

The following is an example configuration:

.. code-block:: yaml

  name: envoy.filters.http.kill_request
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
    probability:
      numerator: 100

