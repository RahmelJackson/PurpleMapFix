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

		const std::size_t anchor = *resetters.rbegin();
		const std::size_t first = *deciders.begin();

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

		// The move set: the map plugins, plus everything between them and the anchor
		// that needs one of them as a master (and so must stay after it).
		std::set<std::string> moving;
		for (auto d : deciders) {
			moving.insert(Lower(loadOrder[d].name));
		}
		for (std::size_t i = first; i <= anchor; ++i) {
			const auto name = Lower(loadOrder[i].name);
			if (moving.contains(name)) {
				continue;
			}
			const bool needsMoved = std::ranges::any_of(loadOrder[i].masters,
				[&](const std::string& m) { return moving.contains(Lower(m)); });
			if (!needsMoved) {
				continue;
			}
			if (resetters.contains(i)) {
				plan.status = Plan::Status::kNotFixable;
				plan.reason = "'" + loadOrder[i].name + "' resets the map but also requires a map plugin as a master, so it has to load after it";
				return plan;
			}
			moving.insert(name);
		}

		for (std::size_t i = first; i <= anchor; ++i) {
			if (moving.contains(Lower(loadOrder[i].name))) {
				plan.move.push_back(loadOrder[i].name);
			}
		}
		plan.after = loadOrder[anchor].name;
		plan.status = Plan::Status::kFixable;
		return plan;
	}
}
