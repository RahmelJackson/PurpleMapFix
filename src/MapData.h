#pragma once

// Reads worldspace map settings straight from plugin files and works out which values
// were changed on purpose. No CommonLib in here (standard library + zlib only), so it
// can be compiled and tested outside the game.
//
// Why: a plugin that edits anything inside a worldspace (places a single object, an NPC,
// a cell change) carries a full copy of that worldspace record, made from whatever its
// masters said. If it loads after a map mod, that stale copy silently resets the map
// mod's settings. For a flat map that puts the camera under the map: a purple sheet.
//
// Rule used here, per worldspace and per field:
//   the base value is the one in the plugin that first defines the worldspace;
//   a later plugin whose value equals the base is just a copy and is ignored;
//   the last plugin whose value differs from the base wins.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace MapData
{
	// Map-related WRLD subrecords handled. MNAM = map bounds + map camera,
	// ONAM = world map offset/scale (what flat maps use to project the world).
	inline constexpr const char* kFields[] = { "MNAM", "ONAM" };
	inline constexpr std::size_t kFieldCount = 2;

	using Bytes = std::vector<std::uint8_t>;

	struct WorldKey
	{
		std::string   originFile;  // lowercase name of the plugin that defines the worldspace
		std::uint32_t objectID;    // FormID without the load-order byte

		bool operator<(const WorldKey& o) const
		{
			return originFile != o.originFile ? originFile < o.originFile : objectID < o.objectID;
		}
	};

	struct WorldEntry
	{
		std::string                      plugin;  // plugin this copy came from
		std::string                      editorID;
		std::optional<Bytes>             fields[kFieldCount];
	};

	struct FieldResult
	{
		Bytes       value;
		std::string decidedBy;  // plugin whose value is used

		// Plugins loading after decidedBy whose copy has a different value, in load
		// order. If this isn't empty, the game would use the wrong value: these are the
		// plugins that "reset" the map setting.
		std::vector<std::string> resetBy;
	};

	struct WorldResult
	{
		WorldKey                   key;
		std::string                editorID;
		std::string                lastOverride;  // plugin whose copy the game uses by default
		std::optional<FieldResult> fields[kFieldCount];
	};

	// Reads one plugin. Returns false (and fills error) if the file can't be read.
	// Every WRLD record found is appended to `out` with its key.
	bool ReadPlugin(const std::string& path, const std::string& pluginName,
		std::vector<std::pair<WorldKey, WorldEntry>>& out, std::string& error);

	// Collects worldspace copies across plugins in load order and resolves them.
	class Resolver
	{
	public:
		void Add(const WorldKey& key, WorldEntry entry);

		// One result per worldspace that more than one plugin touches.
		std::vector<WorldResult> Resolve() const;

	private:
		std::map<WorldKey, std::vector<WorldEntry>> _worlds;
	};

	std::string ToLower(std::string s);
	std::string Hex(const Bytes& b);
}
