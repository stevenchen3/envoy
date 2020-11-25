#include "envoy/registry/registry.h"

#include "extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/wavm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WavmRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createWavmVm(); }

  absl::string_view name() override { return "envoy.wasm.runtime.wavm"; }
  absl::string_view shortName() override { return "wavm"; }
};

#if defined(ENVOY_WASM_WAVM)
REGISTER_FACTORY(WavmRuntimeFactory, WasmRuntimeFactory);
#endif

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
