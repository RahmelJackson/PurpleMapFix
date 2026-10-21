#include "MapData.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include <zlib.h>

namespace MapData
{
	namespace
	{
		constexpr std::uint32_t kCompressed = 0x00040000;
		constexpr std::uint32_t kDeleted = 0x00000020;

		std::uint32_t U32(const std::uint8_t* p)
		{
			std::uint32_t v;
			std::memcpy(&v, p, 4);
			return v;
		}

		std::uint16_t U16(const std::uint8_t* p)
		{
			std::uint16_t v;
			std::memcpy(&v, p, 2);
			return v;
		}

		bool Is(const std::uint8_t* p, const char* sig) { return std::memcmp(p, sig, 4) == 0; }

		// Walk the subrecords of a record body. XXXX gives the size of the next
		// subrecord when it's over 64 KB.
		template <class F>
		void ForEachSubrecord(const Bytes& body, F&& fn)
		{
			std::size_t   i = 0;
			std::uint32_t bigSize = 0;
			bool          haveBig = false;
			while (i + 6 <= body.size()) {
				const std::uint8_t* type = body.data() + i;
				std::uint32_t       len = U16(body.data() + i + 4);
				if (Is(type, "XXXX") && len == 4 && i + 10 <= body.size()) {
					bigSize = U32(body.data() + i + 6);
					haveBig = true;
					i += 10;
					continue;
				}
				if (haveBig) {
					len = bigSize;
					haveBig = false;
				}
				if (i + 6 + len > body.size()) {
					return;  // truncated record: stop rather than read past the end
				}
				fn(type, body.data() + i + 6, len);
				i += 6 + len;
			}
		}

		bool Inflate(const Bytes& packed, Bytes& out)
		{
			if (packed.size() < 4) {
				return false;
			}
			uLongf size = U32(packed.data());
			out.resize(size);
			const int rc = uncompress(out.data(), &size, packed.data() + 4, static_cast<uLong>(packed.size() - 4));
			out.resize(size);
			return rc == Z_OK;
		}

		bool ReadExact(std::ifstream& f, void* dst, std::size_t n)
		{
			f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
			return static_cast<std::size_t>(f.gcount()) == n;
		}
	}

	std::string ToLower(std::string s)
	{
		std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	std::string Hex(const Bytes& b)
	{
		static const char* digits = "0123456789abcdef";
		std::string        out;
		for (auto c : b) {
			out += digits[c >> 4];
			out += digits[c & 15];
		}
		return out;
	}

	bool ReadPlugin(const std::string& path, const std::string& pluginName,
		std::vector<std::pair<WorldKey, WorldEntry>>& out, std::string& error)
	{
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f) {
			error = "can't open file";
			return false;
		}
		// Everything is checked against the real size: seeking past the end of a file
		// isn't an error by itself, so a cut-off file would otherwise go unnoticed.
		const std::streamoff fileSize = f.tellg();
		f.seekg(0);

		// TES4 header: lists the masters, which FormIDs in this file refer to by index.
		std::uint8_t hdr[24];
		if (!ReadExact(f, hdr, 24) || !Is(hdr, "TES4")) {
			error = "not a plugin file";
			return false;
		}
		Bytes headerBody(U32(hdr + 4));
		if (!ReadExact(f, headerBody.data(), headerBody.size())) {
			error = "truncated header";
			return false;
		}
		std::vector<std::string> masters;
		ForEachSubrecord(headerBody, [&](const std::uint8_t* type, const std::uint8_t* data, std::uint32_t len) {
			if (Is(type, "MAST") && len > 0) {
				masters.emplace_back(ToLower(std::string(reinterpret_cast<const char*>(data), strnlen(reinterpret_cast<const char*>(data), len))));
			}
		});
		const std::string self = ToLower(pluginName);

		// Top-level groups. Only the WRLD group is read; everything else is skipped by size.
		while (true) {
			std::uint8_t g[24];
			const auto   groupStart = f.tellg();
			if (!ReadExact(f, g, 24)) {
				break;  // end of file
			}
			if (!Is(g, "GRUP")) {
				error = "unexpected data between groups";
				return false;
			}
			const std::uint32_t groupSize = U32(g + 4);
			const bool          isWorlds = Is(g + 8, "WRLD") && U32(g + 12) == 0;
			const auto          groupEnd = groupStart + static_cast<std::streamoff>(groupSize);
			if (groupSize < 24 || static_cast<std::streamoff>(groupEnd) > fileSize) {
				error = "truncated or damaged file";
				return false;
			}

			if (isWorlds) {
				while (f.tellg() < groupEnd) {
					std::uint8_t h[24];
					if (!ReadExact(f, h, 24)) {
						error = "truncated world group";
						return false;
					}
					if (Is(h, "GRUP")) {
						// cells and children of a worldspace: not needed
						const auto size = static_cast<std::streamoff>(U32(h + 4));
						if (size < 24 || f.tellg() + (size - 24) > groupEnd) {
							error = "truncated or damaged file";
							return false;
						}
						f.seekg(size - 24, std::ios::cur);
						continue;
					}
					const std::uint32_t size = U32(h + 4);
					const std::uint32_t flags = U32(h + 8);
					const std::uint32_t formID = U32(h + 12);
					if (f.tellg() + static_cast<std::streamoff>(size) > groupEnd) {
						error = "truncated or damaged file";
						return false;
					}
					Bytes body(size);
					if (!ReadExact(f, body.data(), size)) {
						error = "truncated record";
						return false;
					}
					if (!Is(h, "WRLD") || (flags & kDeleted)) {
						continue;
					}
					if (flags & kCompressed) {
						Bytes raw;
						if (!Inflate(body, raw)) {
							continue;  // unreadable record: leave it to the game
						}
						body = std::move(raw);
					}

					const std::uint32_t index = formID >> 24;
					WorldKey            key{ index < masters.size() ? masters[index] : self, formID & 0x00FFFFFF };
					WorldEntry          entry;
					entry.plugin = pluginName;
					ForEachSubrecord(body, [&](const std::uint8_t* type, const std::uint8_t* data, std::uint32_t len) {
						if (Is(type, "EDID")) {
							entry.editorID.assign(reinterpret_cast<const char*>(data), strnlen(reinterpret_cast<const char*>(data), len));
						}
						for (std::size_t i = 0; i < kFieldCount; ++i) {
							if (Is(type, kFields[i])) {
								entry.fields[i] = Bytes(data, data + len);
							}
						}
					});
					out.emplace_back(std::move(key), std::move(entry));
				}
			}
			f.clear();
			f.seekg(groupEnd);
		}
		return true;
	}

	void Resolver::Add(const WorldKey& key, WorldEntry entry)
	{
		_worlds[key].push_back(std::move(entry));
	}

	std::vector<WorldResult> Resolver::Resolve() const
	{
		std::vector<WorldResult> results;
		for (const auto& [key, copies] : _worlds) {
			if (copies.size() < 2) {
				continue;  // nobody overrides it, nothing to resolve
			}

			WorldResult r;
			r.key = key;
			r.lastOverride = copies.back().plugin;
			for (const auto& c : copies) {
				if (!c.editorID.empty()) {
					r.editorID = c.editorID;
				}
			}

			for (std::size_t i = 0; i < kFieldCount; ++i) {
				const auto& base = copies.front().fields[i];
				std::optional<FieldResult> decided;
				std::size_t                decidedAt = 0;
				if (base) {
					decided = FieldResult{ *base, copies.front().plugin, {} };
				}
				for (std::size_t n = 1; n < copies.size(); ++n) {
					const auto& v = copies[n].fields[i];
					if (v && v != base) {
						decided = FieldResult{ *v, copies[n].plugin, {} };
						decidedAt = n;
					}
				}
				if (decided) {
					for (std::size_t n = decidedAt + 1; n < copies.size(); ++n) {
						const auto& v = copies[n].fields[i];
						if (v && *v != decided->value) {
							decided->resetBy.push_back(copies[n].plugin);
						}
					}
				}
				r.fields[i] = std::move(decided);
			}
			results.push_back(std::move(r));
		}
		return results;
	}
}
