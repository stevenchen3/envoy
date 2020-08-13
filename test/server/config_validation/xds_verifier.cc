#include "test/server/config_validation/xds_verifier.h"

#include "common/common/logger.h"

namespace Envoy {

XdsVerifier::XdsVerifier(test::server::config_validation::Config::SotwOrDelta sotw_or_delta)
    : num_warming_(0), num_active_(0), num_draining_(0), num_added_(0), num_modified_(0),
      num_removed_(0) {
  if (sotw_or_delta == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = SOTW;
  } else {
    sotw_or_delta_ = DELTA;
  }
  ENVOY_LOG_MISC(debug, "sotw_or_delta_ = {}", sotw_or_delta_);
}

/**
 * get the route referenced by a listener
 */
std::string XdsVerifier::getRoute(const envoy::config::listener::v3::Listener& listener) {
  envoy::config::listener::v3::Filter filter0 = listener.filter_chains()[0].filters()[0];
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager conn_man;
  filter0.typed_config().UnpackTo(&conn_man);
  return conn_man.rds().route_config_name();
}

/**
 * @return true iff the route listener refers to is in all_routes_
 */
bool XdsVerifier::hasRoute(const envoy::config::listener::v3::Listener& listener) {
  return hasRoute(getRoute(listener));
}

bool XdsVerifier::hasRoute(const std::string& name) { return all_routes_.contains(name); }

bool XdsVerifier::hasActiveRoute(const envoy::config::listener::v3::Listener& listener) {
  return hasActiveRoute(getRoute(listener));
}

bool XdsVerifier::hasActiveRoute(const std::string& name) { return active_routes_.contains(name); }

bool XdsVerifier::hasListener(const std::string& name, ListenerState state) {
  return std::any_of(listeners_.begin(), listeners_.end(), [&](const auto& rep) {
    return rep.listener.name() == name && state == rep.state;
  });
}

/**
 * prints the currently stored listeners and their states
 */
void XdsVerifier::dumpState() {
  ENVOY_LOG_MISC(debug, "Listener Dump:");
  for (const auto& rep : listeners_) {
    ENVOY_LOG_MISC(debug, "Name: {}, Route {}, State: {}", rep.listener.name(),
                   getRoute(rep.listener), rep.state);
  }
}

/*
 * if a listener is added for the first time, it will be added as active/warming depending on if
 * envoy knows about its route config
 *
 * if a listener is updated (i.e. there is a already a listener by this name), there are 3 cases:
 * 1. the old listener is active and the new is warming:
 *    - old will remain active
 *    - new will be added as warming, to replace the old when it gets its route
 * 2. the old listener is active and new is active:
 *    - old is drained (seemingly instantaneously)
 *    - new is added as active
 * 3. the old listener is warming and new is active/warming:
 *    - old is completely removed
 *    - new is added as warming/active as normal
 */

/**
 * update a listener when its route is changed, draining/removing the old listener and adding the
 * updated listener
 */
void XdsVerifier::listenerUpdated(const envoy::config::listener::v3::Listener& listener) {
  ENVOY_LOG_MISC(debug, "About to update listener {} to {}", listener.name(), getRoute(listener));
  dumpState();

  if (std::any_of(listeners_.begin(), listeners_.end(), [&](auto& rep) {
        return rep.listener.name() == listener.name() &&
               getRoute(listener) == getRoute(rep.listener) && rep.state != DRAINING;
      })) {
    ENVOY_LOG_MISC(debug, "Ignoring duplicate add of {}", listener.name());
    return;
  }

  bool found = false;
  for (auto it = listeners_.begin(); it != listeners_.end();) {
    const auto& rep = *it;
    ENVOY_LOG_MISC(debug, "checking {} for update", rep.listener.name());
    if (rep.listener.name() == listener.name()) {
      // if we're updating a warming/active listener, num_modified_ must be incremented
      if (rep.state != DRAINING && !found) {
        num_modified_++;
        found = true;
      }

      if (rep.state == ACTIVE) {
        if (hasActiveRoute(listener)) {
          // if the new listener is ready to take traffic, the old listener will be removed
          // it seems to be directly removed without being added to the config dump as draining
          ENVOY_LOG_MISC(debug, "Removing {} after update", listener.name());
          num_active_--;
          it = listeners_.erase(it);
          continue;
        } else {
          // if the new listener has not gotten its route yet, the old listener will remain active
          // until that happens
          ENVOY_LOG_MISC(debug, "Keeping {} as ACTIVE", listener.name());
        }
      } else if (rep.state == WARMING) {
        // if the old listener is warming, it will be removed and replaced with the new
        ENVOY_LOG_MISC(debug, "Removed warming listener {}", listener.name());
        num_warming_--;
        it = listeners_.erase(it);
        // don't increment it
        continue;
      }
    }
    ++it;
  }
  dumpState();
  listenerAdded(listener, true);
}

/**
 * add a new listener to listeners_ in either an active or warming state
 * @param listener the listener to be added
 * @param from_update whether this function was called from listenerUpdated, in which case
 * num_added_ should not be incremented
 */
void XdsVerifier::listenerAdded(const envoy::config::listener::v3::Listener& listener,
                                bool from_update) {
  if (!from_update) {
    num_added_++;
  }

  if (hasActiveRoute(listener)) {
    ENVOY_LOG_MISC(debug, "Adding {} to listeners_ as ACTIVE", listener.name());
    listeners_.push_back({listener, ACTIVE});
    num_active_++;
  } else {
    num_warming_++;
    ENVOY_LOG_MISC(debug, "Adding {} to listeners_ as WARMING", listener.name());
    listeners_.push_back({listener, WARMING});
  }

  ENVOY_LOG_MISC(debug, "listenerAdded({})", listener.name());
  dumpState();
}

/**
 * remove a listener and drain it if it was active
 * @param name the name of the listener to be removed
 */
void XdsVerifier::listenerRemoved(const std::string& name) {
  bool found = false;

  for (auto it = listeners_.begin(); it != listeners_.end();) {
    auto& rep = *it;
    if (rep.listener.name() == name) {
      if (rep.state == ACTIVE) {
        // the listener will be drained before being removed
        ENVOY_LOG_MISC(debug, "Changing {} to DRAINING", name);
        found = true;
        num_active_--;
        num_draining_++;
        rep.state = DRAINING;
      } else if (rep.state == WARMING) {
        // the listener will be removed immediately
        ENVOY_LOG_MISC(debug, "Removed warming listener {}", name);
        found = true;
        num_warming_--;
        it = listeners_.erase(it);
        // don't increment it
        continue;
      }
    }
    ++it;
  }

  if (found) {
    num_removed_++;
  }
}

/**
 * after a SOTW update, see if any listeners that are currently warming can become active
 */
void XdsVerifier::updateSotwListeners() {
  ASSERT(sotw_or_delta_ == SOTW);
  for (auto& rep : listeners_) {
    // check all_routes_, not active_routes_ since this is SOTW, so any inactive routes will become
    // active if this listener refers to them
    if (hasRoute(rep.listener) && rep.state == WARMING) {
      // it should successfully warm now
      ENVOY_LOG_MISC(debug, "Moving {} to ACTIVE state", rep.listener.name());

      // if the route was not originally added as active, change it now
      if (!hasActiveRoute(rep.listener)) {
        std::string route_name = getRoute(rep.listener);
        auto it = all_routes_.find(route_name);
        // all added routes should be in all_routes_ in SOTW
        ASSERT(it != all_routes_.end());
        active_routes_.insert({route_name, it->second});
      }

      // if there were any active listeners that were waiting to be updated, they will now be
      // removed and the warming listener will take their place
      markForRemoval(rep);
      num_warming_--;
      num_active_++;
      rep.state = ACTIVE;
    }
  }
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](auto& listener) { return listener.state == REMOVED; }),
                   listeners_.end());
}

/**
 * after a delta update, update any listeners that refer to the added route
 */
void XdsVerifier::updateDeltaListeners(const envoy::config::route::v3::RouteConfiguration& route) {
  for (auto& rep : listeners_) {
    if (getRoute(rep.listener) == route.name() && rep.state == WARMING) {
      // it should successfully warm now
      ENVOY_LOG_MISC(debug, "Moving {} to ACTIVE state", rep.listener.name());

      // if there were any active listeners that were waiting to be updated, they will now be
      // removed and the warming listener will take their place
      markForRemoval(rep);
      num_warming_--;
      num_active_++;
      rep.state = ACTIVE;
    }
  }
  // erase any active listeners that were replaced
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](auto& listener) { return listener.state == REMOVED; }),
                   listeners_.end());
}

/**
 * @param listener a warming listener that has a corresponding active listener of the same name
 * called after listener receives its route, so it will be moved to active and the old listener will
 * be removed
 */
void XdsVerifier::markForRemoval(ListenerRepresentation& rep) {
  ASSERT(rep.state == WARMING);
  // find the old listener and mark it for removal
  for (auto& old_rep : listeners_) {
    if (old_rep.listener.name() == rep.listener.name() &&
        getRoute(old_rep.listener) != getRoute(rep.listener) && old_rep.state == ACTIVE) {
      // mark it as removed to remove it after the loop so as not to invalidate the iterator in
      // the caller function
      old_rep.state = REMOVED;
      num_active_--;
    }
  }
}

/**
 * add a new route and update any listeners that refer to this route
 */
void XdsVerifier::routeAdded(const envoy::config::route::v3::RouteConfiguration& route) {
  // routes that are not referenced by any resource are ignored, so this creates a distinction
  // between SOTW and delta
  // if an unreferenced route is sent in delta, it is ignored forever as it will not be sent in
  // future RDS updates, whereas in SOTW it will be present in all future RDS updates, so if a
  // listener that refers to it is added in the meantime, it will become active
  if (!hasRoute(route.name())) {
    all_routes_.insert({route.name(), route});
  }

  if (sotw_or_delta_ == DELTA && std::any_of(listeners_.begin(), listeners_.end(), [&](auto& rep) {
        return getRoute(rep.listener) == route.name();
      })) {
    if (!hasActiveRoute(route.name())) {
      active_routes_.insert({route.name(), route});
      updateDeltaListeners(route);
    }
    updateDeltaListeners(route);
  } else if (sotw_or_delta_ == SOTW) {
    updateSotwListeners();
  }
}

/**
 * called after draining a listener, will remove it from listeners_
 */
void XdsVerifier::drainedListener(const std::string& name) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->listener.name() == name && it->state == DRAINING) {
      ENVOY_LOG_MISC(debug, "Drained and removed {}", name);
      num_draining_--;
      listeners_.erase(it);
      return;
    }
  }
  throw EnvoyException(fmt::format("Tried to drain {} which is not draining", name));
}

} // namespace Envoy
