// Tests for the plugin-file reader and resolver (src/MapData.*), using the synthetic
// fixtures from make_fixtures.py. Build and run with tests/run_tests.sh.

#include "MapData.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void Check(bool ok, const std::string& what)
	{
		std::printf("  [%s] %s\n", ok ? "pass" : "FAIL", what.c_str());
		if (!ok) {
			++g_failures;
		}
	}

	const std::string V_MNAM = "0000000000000000e2ff0f002800d8ff0050434700409c4700004842";
	const std::string F_MNAM = "0000000000000000ccff32003400ceff00d683480067d4480000b442";
	const std::string G_MNAM = "0000000000000000ccff32003400ceff00007a480000fa480000b442";
	const std::string N_MNAM = "00000000000000000a00f6ff1400ecff00504347004058470000a042";
	const std::string F_ONAM = "0000000000000000000000003ec23348";

	std::vector<MapData::WorldResult> Run(const std::string& dir, const std::vector<std::string>& order)
	{
		MapData::Resolver resolver;
		for (const auto& name : order) {
			std::vector<std::pair<MapData::WorldKey, MapData::WorldEntry>> found;
			std::string                                                    error;
			if (!MapData::ReadPlugin(dir + "/" + name, name, found, error)) {
				std::printf("  could not read %s: %s\n", name.c_str(), error.c_str());
				++g_failures;
				continue;
			}
			for (auto& [key, entry] : found) {
				resolver.Add(key, std::move(entry));
			}
		}
		return resolver.Resolve();
	}

	const MapData::WorldResult* Find(const std::vector<MapData::WorldResult>& results, const std::string& edid)
	{
		for (const auto& r : results) {
			if (r.editorID == edid) {
				return &r;
			}
		}
		return nullptr;
	}

	std::string Field(const MapData::WorldResult* w, std::size_t i)
	{
		return (w && w->fields[i]) ? MapData::Hex(w->fields[i]->value) : "<none>";
	}

	std::string DecidedBy(const MapData::WorldResult* w, std::size_t i)
	{
		return (w && w->fields[i]) ? w->fields[i]->decidedBy : "<none>";
	}
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : "fixtures";
	constexpr std::size_t MNAM = 0, ONAM = 1;

	std::printf("Stale copies after a map mod (the purple map case)\n");
	{
		auto r = Run(dir, { "Base.esm", "MapMod.esp", "StaleCopy.esp", "StaleCopyZ.esp", "DeletedCopy.esp" });
		auto t = Find(r, "Tamriel");
		Check(t != nullptr, "Tamriel is resolved");
		Check(Field(t, MNAM) == F_MNAM, "map mod's camera wins over later stale copies");
		Check(Field(t, ONAM) == F_ONAM, "map mod's offset wins over later stale copies");
		Check(DecidedBy(t, MNAM) == "MapMod.esp", "decided by MapMod.esp");
		Check(t && t->lastOverride == "StaleCopyZ.esp", "game's own winner is the last stale copy (deleted record ignored)");
		auto c = Find(r, "ChildWorld");
		Check(Field(c, ONAM) == F_ONAM, "child world's offset from the map mod");
		Check(Field(c, MNAM) == "<none>", "child world without MNAM stays without");
	}

	std::printf("A later deliberate change still wins\n");
	{
		auto r = Run(dir, { "Base.esm", "MapMod.esp", "StaleCopy.esp", "LaterMapMod.esp", "StaleCopyZ.esp" });
		auto t = Find(r, "Tamriel");
		Check(Field(t, MNAM) == G_MNAM, "later map mod's camera wins");
		Check(DecidedBy(t, MNAM) == "LaterMapMod.esp", "decided by LaterMapMod.esp");
		Check(Field(t, ONAM) == F_ONAM, "offset unchanged from the map mods");
	}

	std::printf("No map mod: vanilla stays vanilla\n");
	{
		auto r = Run(dir, { "Base.esm", "StaleCopy.esp" });
		auto t = Find(r, "Tamriel");
		Check(Field(t, MNAM) == V_MNAM, "base value kept");
		Check(DecidedBy(t, MNAM) == "Base.esm", "decided by Base.esm");
	}

	std::printf("Worldspace defined by a mod, not a master file\n");
	{
		auto r = Run(dir, { "Base.esm", "NewLand.esp", "NewLandMap.esp", "LandPatch.esp" });
		auto w = Find(r, "NewLandWorld");
		Check(w && w->key.originFile == "newland.esp", "keyed to the plugin that defines it");
		Check(w && w->key.objectID == 0x801, "object ID without the load-order byte");
		Check(Field(w, MNAM) == F_MNAM, "its map mod's camera survives a later stale copy");
		Check(DecidedBy(w, MNAM) == "NewLandMap.esp", "decided by NewLandMap.esp");

		auto r2 = Run(dir, { "Base.esm", "NewLand.esp", "LandPatch.esp" });
		Check(Field(Find(r2, "NewLandWorld"), MNAM) == N_MNAM, "without a map mod, its own value stays");
	}

	std::printf("Damaged file\n");
	{
		std::vector<std::pair<MapData::WorldKey, MapData::WorldEntry>> found;
		std::string                                                    error;
		const bool ok = MapData::ReadPlugin(dir + "/Broken.esp", "Broken.esp", found, error);
		Check(!ok && !error.empty(), "truncated plugin is reported, not crashed on");
		std::string error2;
		Check(!MapData::ReadPlugin(dir + "/DoesNotExist.esp", "x", found, error2), "missing file is reported");
	}

	std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
	return g_failures ? 1 : 0;
}
