#include "PCH.hpp"

// F4SE 0.7.x plugin version block. Declaring the 1.11-family Address Library and layout flags
// marks the plugin version-independent, so F4SE loads it on any 1.11.x runtime (including
// 1.11.240) as long as the matching Address Library database is installed.

namespace
{
	constexpr auto PLUGIN_NAME = "F4MP"sv;
	constexpr auto PLUGIN_AUTHOR = "F4MP contributors"sv;
	constexpr auto PLUGIN_VERSION = REX::Version(F4MP_VERSION_MAJOR, F4MP_VERSION_MINOR, F4MP_VERSION_PATCH, 0);

	constexpr auto RUNTIME_1_11_240 = REL::CreateRuntime(1, 11, 240, 0);
}

F4SE_PLUGIN_VERSION = []() consteval noexcept {
	auto data = F4SE::PluginVersionData();

	data.SetPluginName(PLUGIN_NAME);
	data.SetPluginAuthor(PLUGIN_AUTHOR);
	data.SetPluginVersion(PLUGIN_VERSION);

	data.SetUseSignatureScanning(false);
	data.SetUseAddressLibrary_RuntimeNG(true);
	data.SetUseAddressLibrary_RuntimeAE(true);

	data.SetUseNoStructs(false);
	data.SetIsLayoutDependent_RuntimeNG(true);
	data.SetIsLayoutDependent_RuntimeAE(true);

	data.SetCompatibleVersions(std::array{
		F4SE::RUNTIME_LATEST_NG,
		F4SE::RUNTIME_LATEST_AE,
		RUNTIME_1_11_240,
	});

	// AddTaskPermanent (per-frame ticks) exists since F4SE 0.7.1.
	data.SetMinimumRequiredXSEVersion(REX::Version(0, 7, 1, 0));

	return data;
}();
