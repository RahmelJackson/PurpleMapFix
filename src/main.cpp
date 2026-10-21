// Purple Map Fix
//
// Goal: keep the world map readable when some of its texture files are missing or
// broken, whatever kind of map you use (vanilla 3D, paper 3D, or flat).
//
// Only when the world map is open, this plugin walks the
// loaded world scene graph, which contains the terrain LOD, object LOD and any flat-map
// mesh the map is drawn from. Every texture each mesh points to is checked. A texture
// that is missing, or isn't a valid DDS file, is what the engine draws as purple.
//
// Each broken mesh is sorted into one of these kinds and handled to suit it:
//
//   terrain LOD   the ground of the map           -> patched with a map-style texture
//   object LOD    distant buildings, trees, rocks -> patched, or hidden (configurable)
//   flat map      a big unlit map mesh            -> patched with a map-style texture
//   nearby object a normal object near the player -> patched with a stone texture
//
// "Map style" picks the patch texture: parchment for paper and flat maps, stone for the
// vanilla look. It's detected from your plugin names, or set it yourself in the .ini.
//
// Every broken file is written once to PurpleMapFix.log so the real cause can be fixed.
// Nothing on disk is changed; all patches live in memory for the current session.

#include "pch.h"

#include "LoadOrder.h"
#include "MapData.h"

namespace
{
	using Slot = RE::BSTextureSet::Texture;
	using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using Feature = RE::BSShaderMaterial::Feature;
	using Visit = RE::BSVisit::BSVisitControl;

	std::string ToLower(std::string_view s)
	{
		std::string out(s);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	// ---------------------------------------------------------------- settings

	enum class MapStyle
	{
		kStone,
		kPaper
	};

	enum class ObjectLODMode
	{
		kPatch,
		kHide
	};

	namespace Settings
	{
		bool          restoreMapData = true;
		bool          writeLoadOrderReport = true;
		bool          scanOnMapOpen = true;
		bool          replaceBroken = true;
		bool          patchNearbyObjects = false;
		bool          checkHeaders = false;
		bool          showNotification = true;
		std::uint32_t followUpScans = 2;
		bool          scanWholeWorld = false;
		float         flatMapMinRadius = 4096.0f;

		std::string mapStyle = "auto";       // auto | paper | stone
		std::string objectLODMode = "auto";  // auto | patch | hide

		std::string stoneDiffuse = R"(textures\PurpleMapFix\fallback_d.dds)";
		std::string paperDiffuse = R"(textures\PurpleMapFix\fallback_paper_d.dds)";
		std::string flatNormal = R"(textures\PurpleMapFix\fallback_n.dds)";
		std::string customDiffuse;  // if set, used instead of the style texture on map meshes

		std::string Trim(std::string s)
		{
			auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
			s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
			return s;
		}

		bool ToBool(const std::string& v)
		{
			const auto l = ToLower(v);
			return l == "1" || l == "true" || l == "yes" || l == "on";
		}

		void Load()
		{
			// Skyrim's working directory is the game folder, and mod managers (MO2's VFS,
			// Vortex's links) make the mod's Data files visible at this path.
			std::ifstream file(R"(Data\SKSE\Plugins\PurpleMapFix.ini)");
			if (!file) {
				spdlog::info("No PurpleMapFix.ini found, using defaults");
				return;
			}

			std::string line;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				const auto key = Trim(line.substr(0, eq));
				auto       value = Trim(line.substr(eq + 1));
				if (const auto comment = value.find(';'); comment != std::string::npos) {
					value = Trim(value.substr(0, comment));
				}

				try {
					if (key == "bRestoreMapData") {
						restoreMapData = ToBool(value);
					} else if (key == "bWriteLoadOrderReport") {
						writeLoadOrderReport = ToBool(value);
					} else if (key == "bScanOnMapOpen") {
						scanOnMapOpen = ToBool(value);
					} else if (key == "bReplaceBrokenTextures") {
						replaceBroken = ToBool(value);
					} else if (key == "bPatchNearbyObjects") {
						patchNearbyObjects = ToBool(value);
					} else if (key == "bCheckDDSHeaders") {
						checkHeaders = ToBool(value);
					} else if (key == "bShowNotification") {
						showNotification = ToBool(value);
					} else if (key == "iFollowUpScans") {
						followUpScans = std::min<std::uint32_t>(static_cast<std::uint32_t>(std::stoul(value)), 5);
					} else if (key == "bScanWholeWorld") {
						scanWholeWorld = ToBool(value);
					} else if (key == "fFlatMapMinRadius") {
						flatMapMinRadius = std::stof(value);
					} else if (key == "sMapStyle") {
						mapStyle = ToLower(value);
					} else if (key == "sObjectLODMode") {
						objectLODMode = ToLower(value);
					} else if (key == "sCustomMapTexture") {
						customDiffuse = value;
					} else if (key == "sStoneTexture" && !value.empty()) {
						stoneDiffuse = value;
					} else if (key == "sPaperTexture" && !value.empty()) {
						paperDiffuse = value;
					} else if (key == "sFlatNormal" && !value.empty()) {
						flatNormal = value;
					}
				} catch (...) {
					spdlog::warn("Bad value for {} in PurpleMapFix.ini: '{}'", key, value);
				}
			}
		}
	}

	// Resolved at startup from the settings (and plugin names when set to auto).
	MapStyle      g_style = MapStyle::kStone;
	ObjectLODMode g_objectLODMode = ObjectLODMode::kPatch;

	// Paper and flat map mods almost always say so in their plugin's file name.
	// Returns the name of the first plugin that looks like one, or empty.
	std::string FindMapStylePlugin()
	{
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return {};
		}

		auto looksLikeMapMod = [](RE::TESFile* file) {
			if (!file) {
				return false;
			}
			const auto name = ToLower(file->GetFilename());
			const bool mentionsMap = name.find("map") != std::string::npos;
			return mentionsMap && (name.find("paper") != std::string::npos || name.find("flat") != std::string::npos ||
									  name.find("parchment") != std::string::npos);
		};

		for (auto file : dataHandler->compiledFileCollection.files) {
			if (looksLikeMapMod(file)) {
				return std::string(file->GetFilename());
			}
		}
		for (auto file : dataHandler->compiledFileCollection.smallFiles) {
			if (looksLikeMapMod(file)) {
				return std::string(file->GetFilename());
			}
		}
		return {};
	}

	void ResolveStyle()
	{
		if (Settings::mapStyle == "paper" || Settings::mapStyle == "flat") {
			g_style = MapStyle::kPaper;
			spdlog::info("Map style: paper (set in ini)");
		} else if (Settings::mapStyle == "stone" || Settings::mapStyle == "vanilla") {
			g_style = MapStyle::kStone;
			spdlog::info("Map style: stone (set in ini)");
		} else {
			const auto plugin = FindMapStylePlugin();
			g_style = plugin.empty() ? MapStyle::kStone : MapStyle::kPaper;
			if (plugin.empty()) {
				spdlog::info("Map style: stone (auto, no paper/flat map plugin found)");
			} else {
				spdlog::info("Map style: paper (auto, found {})", plugin);
			}
		}

		if (Settings::objectLODMode == "hide") {
			g_objectLODMode = ObjectLODMode::kHide;
		} else if (Settings::objectLODMode == "patch") {
			g_objectLODMode = ObjectLODMode::kPatch;
		} else {
			// On a paper or flat map a patched building is a blank blob; hiding it reads
			// cleaner. On the vanilla map a gray stand-in looks more natural.
			g_objectLODMode = g_style == MapStyle::kPaper ? ObjectLODMode::kHide : ObjectLODMode::kPatch;
		}
		spdlog::info("Broken object LOD will be {}", g_objectLODMode == ObjectLODMode::kHide ? "hidden" : "patched");
	}

	// ---------------------------------------------------------------- texture checks

	enum class Status
	{
		kOK,
		kMissing,
		kNotDDS
	};

	std::string_view ToString(Status s)
	{
		switch (s) {
		case Status::kMissing:
			return "MISSING"sv;
		case Status::kNotDDS:
			return "NOT A VALID DDS"sv;
		default:
			return "ok"sv;
		}
	}

	// Turn whatever path a mesh stored into the "textures\..." form the resource
	// system expects, lowercase so the cache doesn't hold duplicates.
	std::string NormalizePath(std::string_view raw)
	{
		std::string path = ToLower(raw);
		std::ranges::replace(path, '/', '\\');
		// Paths show up as "data\textures\x", ".\textures\x", "\textures\x" or plain "x".
		if (const auto at = path.find("textures\\"); at != std::string::npos) {
			path.erase(0, at);
		} else {
			while (!path.empty() && (path.front() == '\\' || path.front() == '.' || path.front() == ' ')) {
				path.erase(path.begin());
			}
			path.insert(0, "textures\\");
		}
		return path;
	}

	// Lets the caches be searched with a string_view, so a lookup never allocates.
	struct StringHash
	{
		using is_transparent = void;
		std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
	};
	template <class V>
	using StringMap = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;

	// All scans run on the game's main thread (through the SKSE task queue),
	// so these containers don't need locking.
	StringMap<Status>               g_statusCache;  // keyed by the path exactly as the mesh stores it
	std::unordered_set<std::string> g_alreadyLogged;

	Status CheckTexture(const char* raw)
	{
		if (!raw || std::string_view(raw).find_first_not_of(" \t") == std::string_view::npos) {
			return Status::kOK;  // empty slot: nothing to load, nothing to break
		}

		// Fast path: seen this exact string before. No allocation, one hash.
		if (const auto it = g_statusCache.find(std::string_view(raw)); it != g_statusCache.end()) {
			return it->second;
		}

		const auto path = NormalizePath(raw);

		// BSResourceNiBinaryStream looks in loose files and every loaded BSA,
		// the same way the engine does when it loads the texture for real.
		Status                       status = Status::kOK;
		RE::BSResourceNiBinaryStream stream(path);
		if (!stream.good()) {
			status = Status::kMissing;
		} else if (Settings::checkHeaders) {
			char magic[4]{};
			if (!stream.read(magic, 4) || std::memcmp(magic, "DDS ", 4) != 0) {
				status = Status::kNotDDS;
			}
		}

		g_statusCache.emplace(std::string(raw), status);
		return status;
	}

	bool IsUsable(const std::string& path)
	{
		return !path.empty() && CheckTexture(path.c_str()) == Status::kOK;
	}

	// ---------------------------------------------------------------- mesh kinds

	enum class Kind
	{
		kTerrainLOD,
		kObjectLOD,
		kFlatMap,
		kNearbyObject,
		kSmallEffect
	};

	std::string_view ToString(Kind k)
	{
		switch (k) {
		case Kind::kTerrainLOD:
			return "terrain LOD"sv;
		case Kind::kObjectLOD:
			return "object LOD"sv;
		case Kind::kFlatMap:
			return "flat map"sv;
		case Kind::kNearbyObject:
			return "nearby object"sv;
		default:
			return "effect"sv;
		}
	}

	Kind ClassifyLighting(RE::BSLightingShaderProperty* prop)
	{
		const auto feature = prop->material->GetFeature();
		if (feature == Feature::kLODLand || feature == Feature::kLODLandNoise ||
			feature == Feature::kMultiTexLandLODBlend || prop->flags.any(ShaderFlag::kLODLandscape)) {
			return Kind::kTerrainLOD;
		}
		if (feature == Feature::kLODObjectsHD || prop->flags.any(ShaderFlag::kLODObjects, ShaderFlag::kHDLODObjects)) {
			return Kind::kObjectLOD;
		}
		return Kind::kNearbyObject;
	}

	// Flat world maps are drawn on one huge mesh (or a few huge tiles), usually with an
	// unlit effect shader. Ordinary effect meshes (fire, glows, magic) are small.
	Kind ClassifyEffect(RE::BSGeometry* geo)
	{
		return geo->worldBound.radius >= Settings::flatMapMinRadius ? Kind::kFlatMap : Kind::kSmallEffect;
	}

	// The diffuse texture to use for a broken mesh of this kind, or empty to leave it.
	const std::string& FallbackFor(Kind kind)
	{
		static const std::string none;
		switch (kind) {
		case Kind::kTerrainLOD:
		case Kind::kObjectLOD:
		case Kind::kFlatMap:
			if (IsUsable(Settings::customDiffuse)) {
				return Settings::customDiffuse;
			}
			if (g_style == MapStyle::kPaper || kind == Kind::kFlatMap) {
				if (IsUsable(Settings::paperDiffuse)) {
					return Settings::paperDiffuse;
				}
			}
			return IsUsable(Settings::stoneDiffuse) ? Settings::stoneDiffuse : none;
		case Kind::kNearbyObject:
			if (!Settings::patchNearbyObjects) {
				return none;
			}
			return IsUsable(Settings::stoneDiffuse) ? Settings::stoneDiffuse : none;
		default:
			return none;
		}
	}

	// A short "mesh < parent < grandparent" trail. The parents of LOD meshes are
	// usually named after the LOD block, which helps place the problem.
	std::string DescribeGeometry(RE::BSGeometry* geo)
	{
		std::string     out;
		RE::NiAVObject* obj = geo;
		for (int depth = 0; obj && depth < 4; ++depth, obj = obj->parent) {
			const char* name = obj->name.c_str();
			if (!name || !*name) {
				continue;
			}
			if (!out.empty()) {
				out += " < ";
			}
			out += name;
		}
		return out.empty() ? "<unnamed mesh>"s : out;
	}

	void LogBroken(Status status, std::string_view slot, const char* path, Kind kind, RE::BSGeometry* geo)
	{
		auto key = NormalizePath(path);
		if (g_alreadyLogged.insert(key).second) {
			spdlog::warn("[{}] {} ({}) {}  <- used by: {}", ToString(status), slot, ToString(kind), key,
				DescribeGeometry(geo));
		}
	}

	// ---------------------------------------------------------------- swaps

	// Rebuild a lit mesh's material with a copy of its texture set where the broken
	// slots point to fallback textures. Same approach other texture-swap plugins use:
	// build a temporary material, let SetMaterial copy it, then free the temporary.
	// Many meshes share one broken texture set (a whole LOD block, say), so each broken
	// set is patched once and the patched copy reused. The cache holds a reference to
	// the original set too, so its address can't be reused by a different set.
	struct PatchedSet
	{
		RE::NiPointer<RE::BSTextureSet>       original;
		const std::string*                    fallback;  // which replacement texture it uses
		RE::NiPointer<RE::BSShaderTextureSet> patched;
	};
	std::unordered_map<const RE::BSTextureSet*, PatchedSet> g_patchedSets;

	RE::NiPointer<RE::BSShaderTextureSet> GetPatchedSet(RE::BSTextureSet* oldSet, const std::string& diffuse, bool fixDiffuse, bool fixNormal)
	{
		const auto it = g_patchedSets.find(oldSet);
		if (it != g_patchedSets.end() && it->second.fallback == &diffuse) {
			return it->second.patched;
		}

		RE::NiPointer<RE::BSShaderTextureSet> newSet(RE::BSShaderTextureSet::Create());
		if (!newSet) {
			return {};
		}
		for (std::uint32_t i = 0; i < Slot::kTotal; ++i) {
			const auto slot = static_cast<Slot>(i);
			newSet->SetTexturePath(slot, oldSet->GetTexturePath(slot));
		}
		if (fixDiffuse) {
			newSet->SetTexturePath(Slot::kDiffuse, diffuse.c_str());
		}
		if (fixNormal) {
			newSet->SetTexturePath(Slot::kNormal, Settings::flatNormal.c_str());
		}

		// Same set needing a different replacement (rare): use it, but keep the first in the cache.
		if (it == g_patchedSets.end()) {
			g_patchedSets.emplace(oldSet, PatchedSet{ RE::NiPointer<RE::BSTextureSet>(oldSet), &diffuse, newSet });
		}
		return newSet;
	}

	bool SwapLightingTextures(RE::BSGeometry* geo, RE::BSLightingShaderProperty* prop,
		RE::BSLightingShaderMaterialBase* oldMaterial, RE::BSTextureSet* oldSet,
		const std::string& diffuse, bool fixDiffuse, bool fixNormal)
	{
		const auto newSet = GetPatchedSet(oldSet, diffuse, fixDiffuse, fixNormal);
		if (!newSet) {
			return false;
		}

		auto newMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(oldMaterial->Create());
		if (!newMaterial) {
			return false;
		}

		newMaterial->CopyMembers(oldMaterial);
		newMaterial->ClearTextures();
		newMaterial->OnLoadTextureSet(0, newSet.get());

		prop->SetMaterial(newMaterial, true);
		prop->SetupGeometry(geo);
		prop->FinishSetupGeometry(geo);

		newMaterial->~BSLightingShaderMaterialBase();
		RE::free(newMaterial);
		return true;
	}

	// Unlit (effect shader) meshes keep a single source texture on the material.
	bool SwapEffectTexture(RE::BSGeometry* geo, RE::BSEffectShaderProperty* prop,
		RE::BSEffectShaderMaterial* oldMaterial, const std::string& diffuse)
	{
		RE::NiPointer<RE::NiTexture> loaded;
		RE::BSShaderManager::GetTexture(diffuse.c_str(), true, loaded, false);
		auto source = netimmerse_cast<RE::NiSourceTexture*>(loaded.get());
		if (!source) {
			return false;
		}

		auto newMaterial = static_cast<RE::BSEffectShaderMaterial*>(oldMaterial->Create());
		if (!newMaterial) {
			return false;
		}

		newMaterial->CopyMembers(oldMaterial);
		newMaterial->sourceTexture = RE::NiPointer<RE::NiSourceTexture>(source);
		newMaterial->sourceTexturePath = diffuse.c_str();

		prop->SetMaterial(newMaterial, true);
		prop->SetupGeometry(geo);
		prop->FinishSetupGeometry(geo);

		newMaterial->~BSEffectShaderMaterial();
		RE::free(newMaterial);
		return true;
	}

	// ---------------------------------------------------------------- scanning

	RE::NiAVObject* GetWorldRoot()
	{
		// Same address CommonLib's Main::WorldRootNode() reads. Read directly because
		// that function returns a forward-declared type that can't be used as a node.
		static REL::Relocation<RE::NiPointer<RE::SceneGraph>*> root{ RELOCATION_ID(517006, 403513) };
		return root->get();
	}

	struct ScanStats
	{
		std::uint32_t meshes = 0;
		std::uint32_t checked = 0;
		std::uint32_t broken = 0;
		std::uint32_t patched = 0;
		std::uint32_t hidden = 0;
	};

	// Texture sets are shared by many meshes, so each one is checked once and the
	// result remembered. The diffuse path pointer is stored too, so a set freed and
	// re-created at the same address with different textures gets checked again.
	struct SetVerdict
	{
		const char* diffusePath;
		bool        diffuseBad;
		bool        normalBad;
	};
	std::unordered_map<const RE::BSTextureSet*, SetVerdict> g_setVerdicts;

	void HandleLighting(RE::BSGeometry* geo, RE::BSLightingShaderProperty* prop, ScanStats& stats)
	{
		// Classifying is just a flag check, so do it before touching any textures.
		const auto kind = ClassifyLighting(prop);
		if (kind == Kind::kNearbyObject && !Settings::patchNearbyObjects) {
			return;
		}

		auto material = static_cast<RE::BSLightingShaderMaterialBase*>(prop->material);
		auto set = material->textureSet.get();
		if (!set) {
			return;
		}

		bool diffuseBad = false;
		bool normalBad = false;

		const char* diffusePath = set->GetTexturePath(Slot::kDiffuse);
		if (auto it = g_setVerdicts.find(set); it != g_setVerdicts.end() && it->second.diffusePath == diffusePath) {
			diffuseBad = it->second.diffuseBad;
			normalBad = it->second.normalBad;
		} else {
			++stats.checked;
			// Only diffuse and normal: the slots that turn a mesh purple or black, and the
			// only ones acted on. Checking the other six would just mean more file lookups.
			for (const auto slot : { Slot::kDiffuse, Slot::kNormal }) {
				const auto  i = static_cast<std::uint32_t>(slot);
				const char* path = set->GetTexturePath(slot);
				const auto  status = CheckTexture(path);
				if (status == Status::kOK) {
					continue;
				}
				diffuseBad |= (slot == Slot::kDiffuse);
				normalBad |= (slot == Slot::kNormal);
				LogBroken(status, std::format("slot {}", i), path, kind, geo);
			}
			g_setVerdicts[set] = { diffusePath, diffuseBad, normalBad };
		}

		// Only diffuse and normal matter: those are the slots that make a mesh purple
		// or black. Other bad slots were logged above and are left alone.
		if (!(diffuseBad || normalBad)) {
			return;
		}
		++stats.broken;

		if (!Settings::replaceBroken) {
			return;
		}

		if (kind == Kind::kObjectLOD && g_objectLODMode == ObjectLODMode::kHide) {
			geo->SetAppCulled(true);
			++stats.hidden;
			return;
		}

		const auto& fallback = FallbackFor(kind);
		if (fallback.empty() || !IsUsable(Settings::flatNormal)) {
			return;
		}
		if (SwapLightingTextures(geo, prop, material, set, fallback, diffuseBad, normalBad)) {
			++stats.patched;
		}
	}

	void HandleEffect(RE::BSGeometry* geo, RE::BSEffectShaderProperty* prop, ScanStats& stats)
	{
		// Only huge unlit meshes (a flat world map) are of interest. Small ones are fire,
		// glows and spell effects: never touched, so not even checked.
		const auto kind = ClassifyEffect(geo);
		if (kind != Kind::kFlatMap) {
			return;
		}

		auto material = static_cast<RE::BSEffectShaderMaterial*>(prop->material);
		const char* path = material->sourceTexturePath.c_str();
		++stats.checked;
		const auto status = CheckTexture(path);
		if (status == Status::kOK) {
			return;
		}

		LogBroken(status, "source", path, kind, geo);
		++stats.broken;

		if (!Settings::replaceBroken) {
			return;
		}

		const auto& fallback = FallbackFor(kind);
		if (!fallback.empty() && SwapEffectTexture(geo, prop, material, fallback)) {
			++stats.patched;
		}
	}

	// The world map is drawn from distant terrain and object LOD, which the engine keeps
	// under one node. Scanning just that skips nearby objects, NPCs and grass: most of
	// the meshes in a scene and nearly all of the cost.
	RE::NiAVObject* GetScanRoot()
	{
		if (!Settings::scanWholeWorld) {
			if (auto tes = RE::TES::GetSingleton(); tes && tes->lodLandRoot) {
				return tes->lodLandRoot;
			}
		}
		return GetWorldRoot();
	}

	void ScanAndFix(std::string_view reason)
	{
		auto root = GetScanRoot();
		if (!root) {
			return;
		}

		const auto start = std::chrono::steady_clock::now();
		ScanStats  stats;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geo) {
			++stats.meshes;
			if (geo->GetAppCulled()) {
				return Visit::kContinue;  // hidden already (possibly by us)
			}

			auto  effect = geo->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
			if (auto lit = netimmerse_cast<RE::BSLightingShaderProperty*>(effect); lit && lit->material) {
				HandleLighting(geo, lit, stats);
			} else if (auto unlit = netimmerse_cast<RE::BSEffectShaderProperty*>(effect); unlit && unlit->material) {
				HandleEffect(geo, unlit, stats);
			}
			return Visit::kContinue;
		});

		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

		if (stats.meshes == 0 && !Settings::scanWholeWorld) {
			static bool told = false;
			if (!std::exchange(told, true)) {
				spdlog::warn("No meshes under the LOD node. If broken map textures aren't being caught, set bScanWholeWorld = 1");
			}
		}

		// Log every scan with its cost, so the real CPU time on any setup is visible.
		spdlog::info("Scan ({}): {} meshes, {} new texture sets checked, {} broken, {} patched, {} hidden, {:.2f} ms",
			reason, stats.meshes, stats.checked, stats.broken, stats.patched, stats.hidden, us / 1000.0);

		if ((stats.patched > 0 || stats.hidden > 0) && Settings::showNotification) {
			const auto msg = std::format("Purple Map Fix: patched {}, hid {} (see PurpleMapFix.log)",
				stats.patched, stats.hidden);
			RE::DebugNotification(msg.c_str());
		}
	}

	std::atomic_bool g_scanQueued{ false };

	void QueueScan(std::string reason)
	{
		// Don't pile up scans if the main thread is busy.
		if (g_scanQueued.exchange(true)) {
			return;
		}
		SKSE::GetTaskInterface()->AddTask([reason = std::move(reason)]() {
			g_scanQueued = false;
			ScanAndFix(reason);
		});
	}

	// ---------------------------------------------------------------- map watcher

	std::atomic_bool     g_mapOpen{ false };
	std::atomic_uint32_t g_mapGeneration{ 0 };

	void OnMapOpened()
	{
		g_mapOpen = true;
		const auto generation = ++g_mapGeneration;

		QueueScan("map opened");

		// Some map LOD finishes loading just after the map opens. A couple of short
		// follow-ups catch it; after that nothing runs until the map is opened again.
		// Follow-ups only look at texture sets not seen before, so they're cheap.
		if (Settings::followUpScans == 0) {
			return;
		}
		std::thread([generation]() {
			for (std::uint32_t n = 0; n < Settings::followUpScans; ++n) {
				std::this_thread::sleep_for(std::chrono::milliseconds(n == 0 ? 1000 : 3000));
				if (!g_mapOpen || g_mapGeneration != generation) {
					return;
				}
				QueueScan("map follow-up");
			}
		}).detach();
	}

	class MenuWatcher final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuWatcher* Get()
		{
			static MenuWatcher instance;
			return &instance;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (event && event->menuName == RE::MapMenu::MENU_NAME) {
				if (event->opening) {
					OnMapOpened();
				} else {
					g_mapOpen = false;
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// ---------------------------------------------------------------- map data guard
	//
	// Fixes the purple/blank map caused by load order, not by missing files. See MapData.h.
	// Reads every loaded plugin's worldspace records, works out which map settings were
	// changed on purpose, and puts those back if a later plugin's stale copy reset them.

	// Writes PurpleMapFix-LoadOrder.txt next to the log: which plugins reset the map and
	// the load order change that fixes it for good. "Fix Load Order.bat" applies it.
	void WriteLoadOrderReport(const std::vector<RE::TESFile*>& loaded, const std::vector<LoadOrder::Problem>& problems)
	{
		const auto start = std::chrono::steady_clock::now();
		using Status = LoadOrder::Plan::Status;

		// The plugin list (with every plugin's masters) is only needed when something has
		// to move. In the normal case, nothing is built and the report is a few bytes.
		std::vector<LoadOrder::Plugin> order;
		LoadOrder::Plan                plan;
		if (!problems.empty()) {
			order.reserve(loaded.size());
			for (auto file : loaded) {
				LoadOrder::Plugin p;
				p.name = std::string(file->GetFilename());
				const auto lower = ToLower(p.name);
				p.masterLike = file->recordFlags.any(RE::TESFile::RecordFlag::kMaster) || lower.ends_with(".esm") || lower.ends_with(".esl");
				for (auto master : file->masters) {
					if (master) {
						p.masters.emplace_back(master);
					}
				}
				order.push_back(std::move(p));
			}
			plan = LoadOrder::MakePlan(order, problems);
		}

		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		const auto path = *dir / "PurpleMapFix-LoadOrder.txt";
		std::ofstream out(path, std::ios::trunc);
		if (!out) {
			spdlog::warn("Couldn't write {}", path.string());
			return;
		}

		out << "# Purple Map Fix - load order report\n"
			   "# Written by the game each time it starts. Read by \"Fix Load Order.bat\".\n\n";
		out << "[status]\n"
			<< (plan.status == Status::kFixable ? "fixable" : plan.status == Status::kNotFixable ? "not fixable" : "nothing to do") << "\n";
		if (!plan.reason.empty()) {
			out << "[reason]\n" << plan.reason << "\n";
		}
		auto list = [&](const char* section, const std::vector<std::string>& names) {
			out << "[" << section << "]\n";
			for (const auto& n : names) {
				out << n << "\n";
			}
		};
		if (plan.status != Status::kNothingToDo) {
			list("map plugins", plan.mapPlugins);
			list("reset by", plan.resetBy);
		}
		if (plan.status == Status::kFixable) {
			list("move", plan.move);
			list("masters moved", plan.mastersMoved);
			out << "[before]\n" << plan.before << "\n";
			// Where the game runs from. Mod lists installed with Wabbajack (LoreRim, Nolvus...)
			// keep the game inside the Mod Organizer folder, so the script can find the
			// profiles from here wherever it's run from.
			std::error_code ec;
			const auto gameDir = std::filesystem::current_path(ec);
			if (!ec) {
				out << "[game folder]\n" << gameDir.string() << "\n";
			}
			// The whole load order, so the script can find the profile the game used.
			out << "[loaded]\n";
			for (const auto& p : order) {
				out << p.name << "\n";
			}
		}
		out.close();

		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
		switch (plan.status) {
		case Status::kFixable:
			spdlog::warn("Load order: {} plugin{} load after '{}' and reset the map settings. Purple Map Fix fixed it for this session.",
				plan.resetBy.size(), plan.resetBy.size() == 1 ? "" : "s", plan.before);
			for (const auto& r : plan.resetBy) {
				spdlog::warn("Load order:   resets the map: {}", r);
			}
			for (const auto& m : plan.mastersMoved) {
				spdlog::warn("Load order:   needed as a master, moves up too: {}", m);
			}
			spdlog::warn("Load order: to fix it in your load order for good, close your mod manager and run \"Fix Load Order.bat\" "
						 "(in the mod's \"PurpleMapFix Tools\" folder). It moves {} plugin{} up to just before '{}'. Report written in {:.2f} ms.",
				plan.move.size(), plan.move.size() == 1 ? "" : "s", plan.before, us / 1000.0);
			break;
		case Status::kNotFixable:
			spdlog::warn("Load order: can't be fixed by reordering: {}. Purple Map Fix still fixes it each session.", plan.reason);
			break;
		default:
			break;
		}
	}

	std::uint32_t RestoreMapData()
	{
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return 0;
		}

		const auto start = std::chrono::steady_clock::now();

		// Loaded plugins, in load order.
		std::vector<RE::TESFile*> loaded;
		for (auto file : dataHandler->files) {
			if (file && file->compileIndex != 0xFF) {
				loaded.push_back(file);
			}
		}

		// The file list should already be in load order. If full plugins aren't in
		// ascending index order, sort them so "later" still means later. (Light plugins
		// keep their place relative to each other.)
		const bool inOrder = std::ranges::is_sorted(loaded | std::views::filter([](auto f) { return !f->IsLight(); }),
			{}, [](auto f) { return f->compileIndex; });
		if (!inOrder) {
			spdlog::warn("Map data: plugin list wasn't in load order; sorting by load index");
			std::ranges::stable_sort(loaded, {}, [](auto f) {
				return f->IsLight() ? 0xFE000u + f->smallFileCompileIndex : static_cast<std::uint32_t>(f->compileIndex) << 12;
			});
		}

		// Read the files in parallel: the time is almost all file opens (through the mod
		// manager's virtual file system), which overlap well. Each thread writes only its
		// own slots, and results are combined afterwards in load order.
		struct FileResult
		{
			std::string                                                    name;
			std::vector<std::pair<MapData::WorldKey, MapData::WorldEntry>> found;
			std::string                                                    error;
			bool                                                           ok = false;
		};
		std::vector<FileResult> results(loaded.size());
		for (std::size_t i = 0; i < loaded.size(); ++i) {
			results[i].name = std::string(loaded[i]->GetFilename());
		}

		std::atomic_size_t next{ 0 };
		auto               worker = [&]() {
            for (std::size_t i; (i = next.fetch_add(1)) < results.size();) {
                auto& r = results[i];
                r.ok = MapData::ReadPlugin("Data\\" + r.name, r.name, r.found, r.error);
            }
		};
		const auto threadCount = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 8);
		std::vector<std::thread> threads;
		for (std::size_t t = 1; t < threadCount; ++t) {
			threads.emplace_back(worker);
		}
		worker();  // this thread helps too
		for (auto& t : threads) {
			t.join();
		}

		MapData::Resolver resolver;
		std::uint32_t     unreadable = 0;
		for (auto& r : results) {
			if (!r.ok) {
				++unreadable;
				spdlog::warn("Map data: couldn't read {} ({})", r.name, r.error);
				continue;
			}
			for (auto& [key, entry] : r.found) {
				resolver.Add(key, std::move(entry));
			}
		}

		std::uint32_t                  restored = 0;
		std::vector<LoadOrder::Problem> problems;
		for (const auto& world : resolver.Resolve()) {
			// Most worlds are fine: the game's own copy already has the right values.
			// Only look up the ones that don't (looking a world up by plugin name walks
			// the whole plugin list, so this matters with thousands of plugins).
			const bool needsLook = std::ranges::any_of(world.fields, [](const auto& f) { return f && !f->gameHasIt; });
			if (!needsLook) {
				continue;
			}
			auto ws = dataHandler->LookupForm<RE::TESWorldSpace>(world.key.objectID, world.key.originFile);
			if (!ws) {
				continue;
			}

			struct Target
			{
				void*       data;
				std::size_t size;
				const char* what;
			};
			const Target targets[MapData::kFieldCount] = {
				{ &ws->worldMapData, sizeof(ws->worldMapData), "map bounds and camera (MNAM)" },
				{ &ws->worldMapOffsetData, sizeof(ws->worldMapOffsetData), "map offset and scale (ONAM)" },
			};

			for (std::size_t i = 0; i < MapData::kFieldCount; ++i) {
				const auto& decided = world.fields[i];
				if (!decided || decided->value.empty() || decided->gameHasIt) {
					continue;
				}
				const auto n = std::min(decided->value.size(), targets[i].size);
				if (std::memcmp(targets[i].data, decided->value.data(), n) == 0) {
					continue;  // already right
				}
				std::memcpy(targets[i].data, decided->value.data(), n);
				++restored;

				const auto worldName = world.editorID.empty() ? std::format("{:06X}", world.key.objectID) : world.editorID;
				std::string resetBy;
				for (const auto& r : decided->resetBy) {
					resetBy += (resetBy.empty() ? "'" : ", '") + r + "'";
				}
				if (resetBy.empty()) {
					resetBy = "'" + world.lastOverride + "'";
				}
				spdlog::warn("Map data: {} {} restored from '{}' (it was reset by {}, which load{} later)",
					worldName, targets[i].what, decided->decidedBy, resetBy, decided->resetBy.size() == 1 ? "s" : "");
				if (!decided->resetBy.empty()) {
					problems.push_back({ worldName, decided->decidedBy, decided->resetBy });
				}
			}
		}

		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		spdlog::info("Map data: checked {} plugins in {} ms on {} threads ({} unreadable), {} settings restored",
			loaded.size(), ms, threadCount, unreadable, restored);

		if (Settings::writeLoadOrderReport) {
			WriteLoadOrderReport(loaded, problems);
		}
		return restored;
	}

	// ---------------------------------------------------------------- startup

	void CheckFallbacks()
	{
		auto report = [](std::string_view what, const std::string& path) {
			if (path.empty()) {
				return;
			}
			const auto status = CheckTexture(path.c_str());
			if (status == Status::kOK) {
				spdlog::info("{} texture OK: {}", what, path);
			} else {
				spdlog::error("{} texture {}: {} (is the mod's textures folder installed?)", what, ToString(status), path);
			}
		};
		report("Stone fallback", Settings::stoneDiffuse);
		report("Paper fallback", Settings::paperDiffuse);
		report("Flat normal", Settings::flatNormal);
		report("Custom map", Settings::customDiffuse);
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			if (Settings::restoreMapData) {
				RestoreMapData();
			}
			ResolveStyle();
			CheckFallbacks();
			if (Settings::scanOnMapOpen) {
				if (auto ui = RE::UI::GetSingleton()) {
					ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatcher::Get());
					spdlog::info("Watching for the world map");
				}
			}
			break;
		default:
			break;
		}
	}

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		auto path = *dir / "PurpleMapFix.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		log->set_pattern("[%H:%M:%S] [%l] %v");
		spdlog::set_default_logger(std::move(log));
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();

	spdlog::info("Purple Map Fix loaded");
	Settings::Load();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::error("Could not register with SKSE messaging");
		return false;
	}
	return true;
}
