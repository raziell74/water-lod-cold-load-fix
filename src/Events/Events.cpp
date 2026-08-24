#include "PCH.h"

#include "Events/Events.h"
#include "WaterLOD.h"

namespace
{
	class EventSink final :
		public RE::BSTEventSink<RE::TESLoadGameEvent>,
		public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
	{
	public:
		static EventSink* GetSingleton()
		{
			static EventSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
		{
			WaterLOD::ArmLoad();
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESCellFullyLoadedEvent* a_event,
			RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
		{
			if (a_event && a_event->cell && a_event->cell->IsExteriorCell()) {
				WaterLOD::ArmGrid();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace Events
{
	void Register()
	{
		const auto holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder) {
			return;
		}

		holder->AddEventSink<RE::TESLoadGameEvent>(EventSink::GetSingleton());
		holder->AddEventSink<RE::TESCellFullyLoadedEvent>(EventSink::GetSingleton());
		SKSE::log::info("Registered load and cell-loaded event sinks");
	}
}
