#pragma once

#include "SKSE/Impl/PCH.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

using namespace std::literals;

SKSE_EXPORT constinit SKSE::PluginVersionData SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName("WaterLODColdLoadFix");
	v.PluginVersion({ 1, 0, 0, 0 });
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	return v;
}();

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo) {
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->name = SKSEPlugin_Version.GetPluginName().data();
	pluginInfo->version = SKSEPlugin_Version.GetPluginVersion().pack();
	return true;
}
