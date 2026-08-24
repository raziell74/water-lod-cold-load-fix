#include "PCH.h"

#include "WaterLOD.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr auto kLoadDelay = std::chrono::milliseconds(8000);
	constexpr auto kGridDelay = std::chrono::milliseconds(400);
	constexpr auto kGiveUp = std::chrono::seconds(25);
	constexpr int kMaxDepth = 4;
	constexpr float kCellSize = 4096.0f;

	std::atomic<std::uint32_t> g_generation{ 0 };
	std::atomic<bool> g_holdGrid{ false };
	std::vector<RE::NiPointer<RE::NiAVObject>> g_hidden;

	[[nodiscard]] RE::NiNode* ResolveWaterLODRoot()
	{
		if (const auto tes = RE::TES::GetSingleton(); tes && tes->objLODWaterRoot) {
			return tes->objLODWaterRoot;
		}
		// Function-local: a global RelocationID can init REL::Module during CRT
		// startup, then Module's debug constructor zeros _base (not constinit).
		static REL::Relocation<RE::NiNode**> waterLOD{ REL::RelocationID(516171, 402322) };
		if (const auto p = waterLOD.get(); p && *p) {
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

	void RestoreHidden()
	{
		for (const auto& object : g_hidden) {
			if (object) {
				object->SetAppCulled(false);
			}
		}
		g_hidden.clear();
	}

	void WalkAndCull(RE::NiAVObject* a_object, int a_depth, const std::unordered_set<std::uint64_t>& a_cells, std::uint32_t& a_culled)
	{
		if (!a_object || a_depth > kMaxDepth) {
			return;
		}

		if (a_depth >= 1 && OverlapsAttachedGrid(a_object, a_cells)) {
			a_object->SetAppCulled(true);
			g_hidden.emplace_back(a_object);
			++a_culled;
			return;
		}

		const auto node = a_object->AsNode();
		if (!node) {
			return;
		}

		auto& children = node->GetChildren();
		const auto count = children.capacity();
		for (std::uint16_t i = 0; i < count; ++i) {
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

		const auto restored = static_cast<std::uint32_t>(g_hidden.size());
		RestoreHidden();

		std::uint32_t culled = 0;
		auto& children = root->GetChildren();
		const auto count = children.capacity();
		for (std::uint16_t i = 0; i < count; ++i) {
			if (const auto& child = children[i]; child) {
				WalkAndCull(child.get(), 1, attached, culled);
			}
		}

		SKSE::log::info("Water LOD sync: culled {}, restored {}", culled, restored);
	}

	void Pump(std::uint32_t a_gen, Clock::time_point a_armed, Clock::duration a_delay);

	void Pump(std::uint32_t a_gen, Clock::time_point a_armed, Clock::duration a_delay)
	{
		const auto tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}

		tasks->AddTask([a_gen, a_armed, a_delay]() {
			if (a_gen != g_generation.load(std::memory_order_acquire)) {
				return;
			}
			if (IsMainMenuOpen()) {
				g_holdGrid.store(false, std::memory_order_release);
				return;
			}

			const auto now = Clock::now();
			if (now - a_armed > kGiveUp) {
				SKSE::log::warn("Water LOD cull gave up after {} seconds", kGiveUp.count());
				g_holdGrid.store(false, std::memory_order_release);
				return;
			}

			if (now - a_armed < a_delay || !IsWorldReady()) {
				Pump(a_gen, a_armed, a_delay);
				return;
			}

			CullLeftoverWaterLOD();
			g_holdGrid.store(false, std::memory_order_release);
		});
	}

	void Arm(Clock::duration a_delay)
	{
		const auto gen = g_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
		SKSE::log::debug(
			"Arming water LOD cull in {} ms (generation {})",
			std::chrono::duration_cast<std::chrono::milliseconds>(a_delay).count(),
			gen);
		Pump(gen, Clock::now(), a_delay);
	}
}

namespace WaterLOD
{
	void ArmLoad()
	{
		g_holdGrid.store(true, std::memory_order_release);
		SKSE::log::info("Arming water LOD cull in {} seconds after load", kLoadDelay.count() / 1000);
		Arm(kLoadDelay);
	}

	void ArmGrid()
	{
		if (g_holdGrid.load(std::memory_order_acquire)) {
			return;
		}
		Arm(kGridDelay);
	}

	void OnPreLoadGame()
	{
		g_holdGrid.store(true, std::memory_order_release);
		g_generation.fetch_add(1, std::memory_order_acq_rel);
		g_hidden.clear();
	}

	void OnLoadFailed()
	{
		g_holdGrid.store(false, std::memory_order_release);
	}
}
