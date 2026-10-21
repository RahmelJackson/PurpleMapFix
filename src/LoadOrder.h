#pragma once

// Works out a load order change that fixes the map settings permanently, so the game
// loads the right values without Purple Map Fix having to restore them.
//
// The plugins whose map settings should win (usually FWMF and its patch) are moved to
// just after the last plugin that resets them. Moving them, rather than the plugins
// that reset them, is the smaller change: map plugins only carry map data, and moving a
// plugin later can never put it before one of its masters. Any plugin in between that
// lists a moved plugin as a master moves along with it, keeping its order.
//
// No game code in here, so it can be tested anywhere.

#include <string>
#include <vector>

namespace LoadOrder
{
	struct Plugin
	{
		std::string              name;
		bool                     masterLike = false;  // .esm/.esl or master-flagged: always loads before plain plugins
		std::vector<std::string> masters;
	};

	// One map setting that had to be restored.
	struct Problem
	{
		std::string              world;      // for messages
		std::string              decidedBy;  // plugin whose value should win
		std::vector<std::string> resetBy;    // later plugins that reset it
	};

	struct Plan
	{
		enum class Status
		{
			kNothingToDo,
			kFixable,
			kNotFixable
		};

		Status                   status = Status::kNothingToDo;
		std::string              reason;      // why it can't be fixed by reordering
		std::vector<std::string> mapPlugins;  // the plugins whose values should win
		std::vector<std::string> resetBy;     // every plugin that resets them, in load order
		std::vector<std::string> move;        // plugins to move, in their current order
		std::string              after;       // ...to directly after this plugin
	};

	Plan MakePlan(const std::vector<Plugin>& loadOrder, const std::vector<Problem>& problems);
}
