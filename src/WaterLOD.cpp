#include "PCH.h"

#include "WaterLOD.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
	constexpr int kMaxDepth = 4;
	constexpr float kCellSize = 4096.0f;

	enum class SessionMode : std::uint8_t
	{
		kPending = 0,
		kActive = 1,
		kDisabled = 2
	};

	struct GridAABB
	{
		float minX{ 0.0f };
		float minY{ 0.0f };
		float maxX{ 0.0f };
		float maxY{ 0.0f };
		bool valid{ false };
	};

	std::atomic<std::uint32_t> g_generation{ 0 };
	std::atomic<bool> g_holdGrid{ false };
	std::atomic<bool> g_taskQueued{ false };
	std::atomic<SessionMode> g_session{ SessionMode::kPending };
	std::vector<RE::NiPointer<RE::NiAVObject>> g_hidden;

	[[nodiscard]] bool IsDisabled()
	{
		return g_session.load(std::memory_order_acquire) == SessionMode::kDisabled;
	}

	void DisableForSession()
	{
		auto expected = SessionMode::kPending;
		if (g_session.compare_exchange_strong(expected, SessionMode::kDisabled, std::memory_order_acq_rel)) {
			SKSE::log::info("First save load was interior; disabling water LOD cleanup for this session");
			g_generation.fetch_add(1, std::memory_order_acq_rel);
			g_holdGrid.store(false, std::memory_order_release);
			g_taskQueued.store(false, std::memory_order_release);
		}
	}

	void ActivateForSession()
	{
		auto expected = SessionMode::kPending;
		g_session.compare_exchange_strong(expected, SessionMode::kActive, std::memory_order_acq_rel);
	}

	// Decide from the player's cell after a save load. Pending with no cell yet stays pending.
	[[nodiscard]] bool TryDecideFromPlayerCell()
	{
		const auto mode = g_session.load(std::memory_order_acquire);
		if (mode == SessionMode::kDisabled) {
			return false;
		}
		if (mode == SessionMode::kActive) {
			return true;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return true;
		}

		if (cell->IsInteriorCell()) {
			DisableForSession();
			return false;
		}

		ActivateForSession();
		return !IsDisabled();
	}

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

	[[nodiscard]] GridAABB CollectAttachedGridBounds()
	{
		GridAABB bounds;
		const auto tes = RE::TES::GetSingleton();
		const auto grid = tes ? tes->gridCells : nullptr;
		if (!grid) {
			return bounds;
		}

		std::int32_t minCellX = std::numeric_limits<std::int32_t>::max();
		std::int32_t minCellY = std::numeric_limits<std::int32_t>::max();
		std::int32_t maxCellX = std::numeric_limits<std::int32_t>::min();
		std::int32_t maxCellY = std::numeric_limits<std::int32_t>::min();

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
				minCellX = std::min(minCellX, coords->cellX);
				minCellY = std::min(minCellY, coords->cellY);
				maxCellX = std::max(maxCellX, coords->cellX);
				maxCellY = std::max(maxCellY, coords->cellY);
			}
		}

		if (minCellX > maxCellX) {
			return bounds;
		}

		bounds.minX = static_cast<float>(minCellX) * kCellSize;
		bounds.minY = static_cast<float>(minCellY) * kCellSize;
		bounds.maxX = static_cast<float>(maxCellX + 1) * kCellSize;
		bounds.maxY = static_cast<float>(maxCellY + 1) * kCellSize;
		bounds.valid = true;
		return bounds;
	}

	[[nodiscard]] bool OverlapsAttachedGrid(RE::NiAVObject* a_object, const GridAABB& a_bounds)
	{
		if (!a_object || !a_bounds.valid) {
			return false;
		}

		const auto& pos = a_object->world.translate;
		float radius = a_object->worldBound.radius;
		if (!(radius > 1.0f)) {
			radius = kCellSize * 2.0f;
		}

		const float closestX = std::clamp(pos.x, a_bounds.minX, a_bounds.maxX);
		const float closestY = std::clamp(pos.y, a_bounds.minY, a_bounds.maxY);
		const float dx = pos.x - closestX;
		const float dy = pos.y - closestY;
		return (dx * dx + dy * dy) <= (radius * radius);
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

	void WalkAndCull(RE::NiAVObject* a_object, int a_depth, const GridAABB& a_bounds, std::uint32_t& a_culled)
	{
		if (!a_object || a_depth > kMaxDepth) {
			return;
		}

		if (a_depth >= 1 && OverlapsAttachedGrid(a_object, a_bounds)) {
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
		const auto count = children.free_idx();
		for (std::uint16_t i = 0; i < count; ++i) {
			if (const auto& child = children[i]; child) {
				WalkAndCull(child.get(), a_depth + 1, a_bounds, a_culled);
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

		const auto attached = CollectAttachedGridBounds();
		if (!attached.valid) {
			SKSE::log::info("No attached exterior cells; skip water LOD cull");
			return;
		}

		const auto restored = static_cast<std::uint32_t>(g_hidden.size());
		RestoreHidden();

		std::uint32_t culled = 0;
		auto& children = root->GetChildren();
		const auto count = children.free_idx();
		g_hidden.reserve(count);
		for (std::uint16_t i = 0; i < count; ++i) {
			if (const auto& child = children[i]; child) {
				WalkAndCull(child.get(), 1, attached, culled);
			}
		}

		SKSE::log::info("Water LOD sync: culled {}, restored {}", culled, restored);
	}

	void RequestSync()
	{
		if (IsDisabled()) {
			return;
		}
		if (g_taskQueued.exchange(true, std::memory_order_acq_rel)) {
			return;
		}

		const auto tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			g_taskQueued.store(false, std::memory_order_release);
			return;
		}

		const auto gen = g_generation.load(std::memory_order_acquire);
		tasks->AddTask([gen]() {
			g_taskQueued.store(false, std::memory_order_release);
			if (gen != g_generation.load(std::memory_order_acquire) || IsDisabled()) {
				return;
			}
			if (IsMainMenuOpen()) {
				g_holdGrid.store(false, std::memory_order_release);
				return;
			}
			if (!TryDecideFromPlayerCell()) {
				return;
			}
			if (!IsWorldReady()) {
				const auto player = RE::PlayerCharacter::GetSingleton();
				const auto cell = player ? player->GetParentCell() : nullptr;
				if (cell && cell->IsInteriorCell()) {
					g_holdGrid.store(false, std::memory_order_release);
				}
				return;
			}

			CullLeftoverWaterLOD();
			g_holdGrid.store(false, std::memory_order_release);
		});
	}
}

namespace WaterLOD
{
	void ArmLoad()
	{
		if (IsDisabled() || !TryDecideFromPlayerCell()) {
			return;
		}
		g_holdGrid.store(true, std::memory_order_release);
		SKSE::log::info("Requesting water LOD cull after load");
		RequestSync();
	}

	void ArmGrid()
	{
		if (IsDisabled() || g_holdGrid.load(std::memory_order_acquire)) {
			return;
		}
		RequestSync();
	}

	void OnPreLoadGame()
	{
		if (IsDisabled()) {
			return;
		}
		g_holdGrid.store(true, std::memory_order_release);
		g_generation.fetch_add(1, std::memory_order_acq_rel);
		g_taskQueued.store(false, std::memory_order_release);
		g_hidden.clear();
	}

	void OnLoadFailed()
	{
		g_holdGrid.store(false, std::memory_order_release);
	}
}
