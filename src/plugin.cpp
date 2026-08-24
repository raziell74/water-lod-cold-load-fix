#include "PCH.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <unordered_set>

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr auto kDelay = std::chrono::seconds(8);
	constexpr auto kGiveUp = std::chrono::seconds(25);
	constexpr int kMaxDepth = 4;
	constexpr float kCellSize = 4096.0f;

	std::atomic<std::uint32_t> g_generation{ 0 };

	REL::Relocation<RE::NiNode**> g_waterLOD{ REL::RelocationID(516171, 402322) };

	[[nodiscard]] RE::NiNode* ResolveWaterLODRoot()
	{
		if (const auto tes = RE::TES::GetSingleton(); tes && tes->objLODWaterRoot) {
			return tes->objLODWaterRoot;
		}
		if (const auto p = g_waterLOD.get(); p && *p) {
			return *p;
		}
		return nullptr;
	}

	[[nodiscard]] bool IsWorldReady()
	{
		const auto ui = RE::UI::GetSingleton();
		if (!ui || ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
			return false;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto cell = player ? player->GetParentCell() : nullptr;
		return player && player->Get3D() && cell && cell->IsExteriorCell();
	}

	[[nodiscard]] bool IsMainMenuOpen()
	{
		const auto ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
	}

	[[nodiscard]] bool CircleOverlapsCell(float a_x, float a_y, float a_radius, std::int32_t a_cellX, std::int32_t a_cellY)
	{
		const float minX = static_cast<float>(a_cellX) * kCellSize;
		const float minY = static_cast<float>(a_cellY) * kCellSize;
		const float maxX = minX + kCellSize;
		const float maxY = minY + kCellSize;
		const float closestX = std::clamp(a_x, minX, maxX);
		const float closestY = std::clamp(a_y, minY, maxY);
		const float dx = a_x - closestX;
		const float dy = a_y - closestY;
		return (dx * dx + dy * dy) <= (a_radius * a_radius);
	}

	[[nodiscard]] std::unordered_set<std::uint64_t> CollectAttachedExteriorCells()
	{
		std::unordered_set<std::uint64_t> cells;
		const auto tes = RE::TES::GetSingleton();
		const auto grid = tes ? tes->gridCells : nullptr;
		if (!grid) {
			return cells;
		}

		for (std::uint32_t x = 0; x < grid->length; ++x) {
			for (std::uint32_t y = 0; y < grid->length; ++y) {
				const auto cell = grid->GetCell(x, y);
				if (!cell || !cell->IsAttached() || !cell->IsExteriorCell()) {
					continue;
				}
				const auto coords = cell->GetCoordinates();
				if (!coords) {
					continue;
				}
				const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coords->cellX)) << 32) |
				                 static_cast<std::uint32_t>(coords->cellY);
				cells.insert(key);
			}
		}
		return cells;
	}

	[[nodiscard]] bool OverlapsAttachedGrid(RE::NiAVObject* a_object, const std::unordered_set<std::uint64_t>& a_cells)
	{
		if (!a_object || a_cells.empty()) {
			return false;
		}

		const auto& pos = a_object->world.translate;
		float radius = a_object->worldBound.radius;
		if (!(radius > 1.0f)) {
			radius = kCellSize * 2.0f;
		}

		for (const auto key : a_cells) {
			const auto cellX = static_cast<std::int32_t>(key >> 32);
			const auto cellY = static_cast<std::int32_t>(key & 0xFFFFFFFFu);
			if (CircleOverlapsCell(pos.x, pos.y, radius, cellX, cellY)) {
				return true;
			}
		}
		return false;
	}

	void WalkAndCull(RE::NiAVObject* a_object, int a_depth, const std::unordered_set<std::uint64_t>& a_cells, std::uint32_t& a_culled)
	{
		if (!a_object || a_depth > kMaxDepth) {
			return;
		}

		if (a_depth >= 1 && OverlapsAttachedGrid(a_object, a_cells)) {
			a_object->SetAppCulled(true);
			++a_culled;
			return;
		}

		const auto node = a_object->AsNode();
		if (!node) {
			return;
		}

		auto& children = node->GetChildren();
		const auto count = children.capacity();
		for (std::uint32_t i = 0; i < count; ++i) {
			if (const auto& child = children[i]; child) {
				WalkAndCull(child.get(), a_depth + 1, a_cells, a_culled);
			}
		}
	}

	void CullLeftoverWaterLOD()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto cell = player ? player->GetParentCell() : nullptr;
		if (!cell || cell->IsInteriorCell()) {
			return;
		}

		RE::NiPointer<RE::NiNode> root{ ResolveWaterLODRoot() };
		if (!root) {
			SKSE::log::warn("Water LOD root was null; skip cull");
			return;
		}

		if (const auto waterSystem = RE::TESWaterSystem::GetSingleton();
			waterSystem && waterSystem->waterRoot.get() == root.get()) {
			SKSE::log::warn("Resolved near-water root instead of LOD root; skip cull");
			return;
		}

		const auto attached = CollectAttachedExteriorCells();
		if (attached.empty()) {
			SKSE::log::info("No attached exterior cells; skip water LOD cull");
			return;
		}

		std::uint32_t culled = 0;
		auto& children = root->GetChildren();
		const auto count = children.capacity();
		for (std::uint32_t i = 0; i < count; ++i) {
			if (const auto& child = children[i]; child) {
				WalkAndCull(child.get(), 1, attached, culled);
			}
		}

		SKSE::log::info("Culled {} leftover water LOD node(s)", culled);
	}

	void Pump(std::uint32_t a_gen, Clock::time_point a_armed);

	void Pump(std::uint32_t a_gen, Clock::time_point a_armed)
	{
		const auto tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}

		tasks->AddTask([a_gen, a_armed]() {
			if (a_gen != g_generation.load(std::memory_order_acquire)) {
				return;
			}
			if (IsMainMenuOpen()) {
				return;
			}

			const auto now = Clock::now();
			if (now - a_armed > kGiveUp) {
				SKSE::log::warn("Water LOD cull gave up after {} seconds", kGiveUp.count());
				return;
			}

			if (now - a_armed < kDelay || !IsWorldReady()) {
				Pump(a_gen, a_armed);
				return;
			}

			CullLeftoverWaterLOD();
		});
	}

	void ArmDelay()
	{
		const auto gen = g_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
		SKSE::log::info("Arming water LOD cull in {} seconds (generation {})", kDelay.count(), gen);
		Pump(gen, Clock::now());
	}

	class LoadSink final : public RE::BSTEventSink<RE::TESLoadGameEvent>
	{
	public:
		static LoadSink* GetSingleton()
		{
			static LoadSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
		{
			ArmDelay();
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
				holder->AddEventSink<RE::TESLoadGameEvent>(LoadSink::GetSingleton());
				SKSE::log::info("Registered TESLoadGameEvent sink");
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			g_generation.fetch_add(1, std::memory_order_acq_rel);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			if (a_msg->data && *static_cast<bool*>(a_msg->data)) {
				ArmDelay();
			}
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	SKSE::log::info("{} v{} loaded", plugin->GetName(), plugin->GetVersion().string());

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::error("Failed to register SKSE messaging listener");
		return false;
	}

	return true;
}
