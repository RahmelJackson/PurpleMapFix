// Tests for the load order planner (src/LoadOrder.*). Build and run with tests/run_tests.sh.

#include "LoadOrder.h"

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

	using Names = std::vector<std::string>;
	using LoadOrder::Plan;
	using LoadOrder::Plugin;
	using LoadOrder::Problem;

	Plugin P(std::string name, Names masters = {}, bool master = false)
	{
		return Plugin{ std::move(name), master, std::move(masters) };
	}
}

int main()
{
	std::printf("LoreRim's case: map plugins, then mods that reset them\n");
	{
		std::vector<Plugin> lo = {
			P("Skyrim.esm", {}, true), P("Dragonborn.esm", { "Skyrim.esm" }, true),
			P("FWMF for Fantasy Paper Maps.esp", { "Skyrim.esm" }),
			P("DLC1MapMarkerEnabler.esp", { "Skyrim.esm" }),
			P("FWMF - Ultimate Patch.esp", { "Skyrim.esm", "Dragonborn.esm" }),
			P("SomeArmor.esp", { "Skyrim.esm" }),
			P("EldenSkyrim_RimSkills.esp", { "Skyrim.esm" }),
			P("MapAddon.esp", { "FWMF - Ultimate Patch.esp" }),
			P("ahzFleetKnight.esp", { "Skyrim.esm" }),
			P("LastArmor.esp", { "Skyrim.esm" }),
		};
		std::vector<Problem> problems = {
			{ "Tamriel", "FWMF - Ultimate Patch.esp", { "ahzFleetKnight.esp" } },
			{ "DLC2SolstheimWorld", "FWMF - Ultimate Patch.esp", { "EldenSkyrim_RimSkills.esp" } },
		};
		auto plan = LoadOrder::MakePlan(lo, problems);
		Check(plan.status == Plan::Status::kFixable, "fixable");
		Check(plan.mapPlugins == Names{ "FWMF - Ultimate Patch.esp" }, "map plugin identified");
		Check(plan.resetBy == Names{ "EldenSkyrim_RimSkills.esp", "ahzFleetKnight.esp" }, "both resetting plugins listed, in load order");
		Check(plan.after == "ahzFleetKnight.esp", "moves after the last resetting plugin");
		Check(plan.move == Names{ "FWMF - Ultimate Patch.esp", "MapAddon.esp" }, "a plugin that needs the map plugin as a master moves with it");
	}

	std::printf("Nothing to do\n");
	{
		auto plan = LoadOrder::MakePlan({ P("Skyrim.esm", {}, true) }, {});
		Check(plan.status == Plan::Status::kNothingToDo, "no problems, no plan");
	}

	std::printf("Map plugin is a master file\n");
	{
		std::vector<Plugin> lo = { P("Skyrim.esm", {}, true), P("MapMaster.esm", { "Skyrim.esm" }, true), P("Armor.esp", { "Skyrim.esm" }) };
		auto plan = LoadOrder::MakePlan(lo, { { "Tamriel", "MapMaster.esm", { "Armor.esp" } } });
		Check(plan.status == Plan::Status::kNotFixable, "not fixable by reordering");
		Check(!plan.reason.empty(), "says why");
	}

	std::printf("Resetting plugin depends on the map plugin\n");
	{
		std::vector<Plugin> lo = { P("Skyrim.esm", {}, true), P("Map.esp", { "Skyrim.esm" }), P("Odd.esp", { "Map.esp" }) };
		auto plan = LoadOrder::MakePlan(lo, { { "Tamriel", "Map.esp", { "Odd.esp" } } });
		Check(plan.status == Plan::Status::kNotFixable, "not fixable: it must stay after its master");
	}

	std::printf("Names differ in case between the game and the list\n");
	{
		std::vector<Plugin> lo = { P("Skyrim.esm", {}, true), P("Map.esp", { "skyrim.esm" }), P("Armor.esp", { "Skyrim.esm" }) };
		auto plan = LoadOrder::MakePlan(lo, { { "Tamriel", "MAP.ESP", { "armor.esp" } } });
		Check(plan.status == Plan::Status::kFixable && plan.after == "Armor.esp" && plan.move == Names{ "Map.esp" }, "matched case-insensitively");
	}

	std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
	return g_failures ? 1 : 0;
}
