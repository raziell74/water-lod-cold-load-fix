#include "PCH.h"

#include "Events/Events.h"
#include "Hooks/Hooks.h"
#include "WaterLOD.h"

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Events::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			WaterLOD::OnPreLoadGame();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			// SKSE dispatches (void*)result, not a bool*. Success is pointer value 1.
			if (a_msg->data) {
				WaterLOD::ArmLoad();
			} else {
				WaterLOD::OnLoadFailed();
			}
			break;
		default:
			break;
		}
	}
}

namespace Hooks
{
	bool Register()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
			SKSE::log::error("Failed to register SKSE messaging listener");
			return false;
		}
		return true;
	}
}
