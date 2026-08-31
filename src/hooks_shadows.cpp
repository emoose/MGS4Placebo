#include "hook_mgr.hpp"
#include "plugin.hpp"

namespace Settings
{
	Setting<int> ShadowBufferSize("Shadows", "BufferSize", 8192,
		"Resolution of each shadow map, overriding what the config file asks "
		"for. 0 leaves the game's own value alone.",
		Settings::Range<int>{ 0, 16384 });

	Setting<int> ShadowSampleCount("Shadows", "SampleCount", 16,
		"Number of samples taken when filtering a shadow map, overriding what "
		"the config file asks for. 0 leaves the game's own value alone.",
		Settings::Range<int>{ 0, 256 });
};

// Shadow settings arrive as text entries whose key looks like
// "Shadow...@<index>.<field>". The parser walks those entries, pulls the index
// out from between the '@' and the '.', matches the field name, and appends one
// fixed size record per entry to a vector its caller then loads into a map
// keyed by the index. Rewriting the values in that vector is enough to change
// the settings, and avoids having to know the key text or the map layout.
namespace
{
	// One parsed entry, 24 bytes, built at 0x142BEA and appended at 0x142C03.
	struct ShadowEntry
	{
		int32_t field;
		int32_t index;
		int32_t unknown[2];
		int32_t value;
		int32_t padding;
	};
	static_assert(sizeof(ShadowEntry) == 24, "record size is fixed by the game");

	// Values the parser writes into ShadowEntry::field, one per key it matches.
	constexpr int32_t kFieldSampleCount = 4;
	constexpr int32_t kFieldBufferSize = 5;

	// The std::vector the parser appends to, as its three raw pointers.
	struct ShadowEntryVector
	{
		ShadowEntry* first;
		ShadowEntry* last;
		ShadowEntry* capacity;
	};

	using ParseShadowSettings_fn = void*(__fastcall*)(void* source, ShadowEntryVector* entries);
}

class ShadowSettingsHook : public Hook
{
	inline static SafetyHookInline ParseShadowSettings_hook = {};

	static void* ParseShadowSettings_dest(void* source, ShadowEntryVector* entries)
	{
		void* result = ParseShadowSettings_hook.call<void*>(source, entries);

		const int32_t bufferSize = Settings::ShadowBufferSize;
		const int32_t sampleCount = Settings::ShadowSampleCount;

		for (ShadowEntry* entry = entries->first; entry != entries->last; entry++)
		{
			if (entry->field == kFieldBufferSize && bufferSize > 0)
			{
				spdlog::info("ShadowSettingsHook: shadow {} buffer size {} -> {}",
					entry->index, entry->value, bufferSize);
				entry->value = bufferSize;
			}
			else if (entry->field == kFieldSampleCount && sampleCount > 0)
			{
				spdlog::info("ShadowSettingsHook: shadow {} sample count {} -> {}",
					entry->index, entry->value, sampleCount);
				entry->value = sampleCount;
			}
		}

		return result;
	}

public:
	std::string_view description() override
	{
		return "ShadowSettingsHook";
	}

	bool validate() override
	{
		return Settings::ShadowBufferSize > 0 || Settings::ShadowSampleCount > 0;
	}

	void declare_settings() override
	{
		// The settings are read once while the config is parsed during startup.
		Settings::ShadowBufferSize.needs_restart();
		Settings::ShadowSampleCount.needs_restart();
	}

	bool apply() override
	{
		//   mov [rsp+8], rbx / mov [rsp+10h], rsi
		if (!Module::code_matches(0x1428D0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 }))
			return false;

		ParseShadowSettings_hook = safetyhook::create_inline(
			Module::exe_ptr(0x1428D0), ParseShadowSettings_dest);

		return bool(ParseShadowSettings_hook);
	}

	static ShadowSettingsHook instance;
};
ShadowSettingsHook ShadowSettingsHook::instance;
