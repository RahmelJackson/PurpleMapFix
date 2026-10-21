#pragma once

// Works out a load order change that fixes the map settings permanently, so the game
// loads the right values without Purple Map Fix having to restore them.
//
// The plugins that reset the map settings move up to just before the first map plugin
// (the plugin whose values should win, usually FWMF's patch), keeping their order. A
// plugin can't load before its own masters, so any master of a moved plugin that loads
// after that point moves up with it. If a plugin needs a map plugin as a master, it
// can't move before it, and the plan says it can't be fixed by reordering.
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
		std::vector<std::string> move;        // plugins to move up, in their current order
		std::vector<std::string> mastersMoved;  // the part of `move` that are only there as masters
		std::string              before;      // ...to directly before this plugin
	};

	Plan MakePlan(const std::vector<Plugin>& loadOrder, const std::vector<Problem>& problems);
}
