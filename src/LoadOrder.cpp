#include "LoadOrder.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>

namespace LoadOrder
{
	namespace
	{
		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

	Plan MakePlan(const std::vector<Plugin>& loadOrder, const std::vector<Problem>& problems)
	{
		Plan plan;
		if (problems.empty()) {
			return plan;
		}

		std::unordered_map<std::string, std::size_t> position;
		for (std::size_t i = 0; i < loadOrder.size(); ++i) {
			position.emplace(Lower(loadOrder[i].name), i);
		}
		auto find = [&](const std::string& name) -> long long {
			const auto it = position.find(Lower(name));
			return it == position.end() ? -1 : static_cast<long long>(it->second);
		};

		std::set<std::size_t> deciders;
		std::set<std::size_t> resetters;
		for (const auto& p : problems) {
			const auto d = find(p.decidedBy);
			if (d < 0) {
				plan.status = Plan::Status::kNotFixable;
				plan.reason = "'" + p.decidedBy + "' isn't in the load order";
				return plan;
			}
			deciders.insert(static_cast<std::size_t>(d));
			for (const auto& r : p.resetBy) {
				const auto rp = find(r);
				if (rp >= 0) {
					resetters.insert(static_cast<std::size_t>(rp));
				}
			}
		}
		if (resetters.empty()) {
			return plan;
		}

		for (auto i : deciders) {
			plan.mapPlugins.push_back(loadOrder[i].name);
		}
		for (auto i : resetters) {
			plan.resetBy.push_back(loadOrder[i].name);
		}

		const std::size_t anchor = *deciders.begin();  // the first map plugin

		// Masters always load before plain plugins, whatever the list says. If a map
		// plugin is a master and a plugin resetting it isn't, no order can fix it.
		for (auto d : deciders) {
			for (auto r : resetters) {
				if (r > d && loadOrder[d].masterLike && !loadOrder[r].masterLike) {
					plan.status = Plan::Status::kNotFixable;
					plan.reason = "'" + loadOrder[d].name + "' is a master file, and master files always load before '" +
					              loadOrder[r].name + "', whatever the plugin order says";
					return plan;
				}
			}
		}

		// The move set: the resetting plugins, plus every master they need that loads
		// after the anchor (and those masters' masters, and so on).
		std::set<std::size_t> moving;
		std::vector<std::size_t> pending(resetters.begin(), resetters.end());
		while (!pending.empty()) {
			const auto i = pending.back();
			pending.pop_back();
			if (i <= anchor || !moving.insert(i).second) {
				continue;  // already loads early enough, or already handled
			}
			for (const auto& m : loadOrder[i].masters) {
				const auto mp = find(m);
				if (mp < 0) {
					continue;  // not loaded: the game wouldn't load this plugin either
				}
				const auto mi = static_cast<std::size_t>(mp);
				if (deciders.contains(mi)) {
					plan.status = Plan::Status::kNotFixable;
					plan.reason = "'" + loadOrder[i].name + "' needs the map plugin '" + loadOrder[mi].name +
					              "' as a master, so it has to load after it";
					return plan;
				}
				if (mi > anchor) {
					pending.push_back(mi);
				}
			}
		}

		for (auto i : moving) {
			plan.move.push_back(loadOrder[i].name);
			if (!resetters.contains(i)) {
				plan.mastersMoved.push_back(loadOrder[i].name);
			}
		}
		plan.before = loadOrder[anchor].name;
		plan.status = Plan::Status::kFixable;
		return plan;
	}
}
