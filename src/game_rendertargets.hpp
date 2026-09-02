#pragma once

// The engine's render target table.
//
// Every render target the engine creates has a descriptor in one fixed table, 
// and the engine addresses a target by its index into that table.

#include <cstddef>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

#include "plugin.hpp"
#include "game_bgfx.hpp"

namespace RenderTargets
{
	// The engine's RT descriptor, as laid out by its allocator.
	struct Desc
	{
		uint8_t _unk00[0x20];
		uint32_t usage;   // +0x20
		uint16_t width;   // +0x24
		uint16_t height;  // +0x26
		uint8_t _unk28[0x08];
		int32_t format;   // +0x30
		uint16_t handle;  // +0x34
		uint8_t _unk36[0x3A];
	};
	static_assert(sizeof(Desc) == 112);
	static_assert(offsetof(Desc, usage) == 0x20);
	static_assert(offsetof(Desc, width) == 0x24);
	static_assert(offsetof(Desc, height) == 0x26);
	static_assert(offsetof(Desc, format) == 0x30);
	static_assert(offsetof(Desc, handle) == 0x34);

	constexpr size_t kCount = 0x1421;

	// The engine builds its texture flags in a 32-bit value and shifts that left
	// by this to make bgfx's 64-bit flags, so each bgfx flag has a counterpart
	// here the same number of bits lower.
	constexpr uint32_t kEngineFlagShift = 34;

	// The render target mode field, which selects no-MSAA or a sample count.
	constexpr uint32_t kRtFieldShift = BGFX_TEXTURE_RT_MSAA_SHIFT - kEngineFlagShift;
	constexpr uint32_t kRtFieldMask = uint32_t(BGFX_TEXTURE_RT_MSAA_MASK >> kEngineFlagShift);

	// The field value the game always produces: a render target with no MSAA.
	constexpr uint32_t kRtPlain = uint32_t(BGFX_TEXTURE_RT >> BGFX_TEXTURE_RT_MSAA_SHIFT);

	// "Never sampled", which lets the renderer skip the resolve it cannot
	// perform on a multisampled depth surface.
	constexpr uint32_t kWriteOnlyBit = uint32_t(BGFX_TEXTURE_RT_WRITE_ONLY >> kEngineFlagShift);

	// Asks the renderer to keep the target readable rather than resolving it,
	// which is the only way a multisampled depth buffer can be read at all:
	// Direct3D has no resolve for depth formats.
	constexpr uint32_t kMsaaSampleBit = uint32_t(BGFX_TEXTURE_MSAA_SAMPLE >> kEngineFlagShift);

	// The values the game was measured to use, so a wrong kEngineFlagShift is a
	// build error rather than flags that look plausible.
	static_assert(kRtFieldShift == 2 && kRtFieldMask == 0x1C && kRtPlain == 1);
	static_assert(kWriteOnlyBit == 0x20 && kMsaaSampleBit == 0x02);

	// The mode field for a sample count, or 0 for one bgfx cannot express.
	inline uint32_t RtFieldForSamples(int samples)
	{
		switch (samples)
		{
		case 2:  return uint32_t(BGFX_TEXTURE_RT_MSAA_X2 >> BGFX_TEXTURE_RT_MSAA_SHIFT);
		case 4:  return uint32_t(BGFX_TEXTURE_RT_MSAA_X4 >> BGFX_TEXTURE_RT_MSAA_SHIFT);
		case 8:  return uint32_t(BGFX_TEXTURE_RT_MSAA_X8 >> BGFX_TEXTURE_RT_MSAA_SHIFT);
		case 16: return uint32_t(BGFX_TEXTURE_RT_MSAA_X16 >> BGFX_TEXTURE_RT_MSAA_SHIFT);
		default: return 0;
		}
	}

	// The size the scene is being rendered at, which the engine's own getters
	// return and which every full size render target is created at.
	inline int32_t RenderWidth() { return *Module::exe_ptr<int32_t>(0x1B00000); }
	inline int32_t RenderHeight() { return *Module::exe_ptr<int32_t>(0x1B00004); }

	inline Desc* Table() { return Module::exe_ptr<Desc>(0x23BE75F0); }

	inline bool InTable(const Desc* desc)
	{
		const Desc* table = Table();
		return desc >= table && desc < table + kCount;
	}

	inline uint32_t IndexOf(const Desc* desc) { return uint32_t(desc - Table()); }

	// Everything from UnknownDepth up in bgfx's format list is a depth format.
	inline bool IsDepth(const Desc* desc) { return desc->format >= bgfx::TextureFormat::UnknownDepth; }

	// Finds the target a texture handle came from. Handles are handed out again
	// once a target is destroyed, so a stale one can match a slot that has since
	// moved on: only good enough for reporting, never for deciding anything.
	inline Desc* FromHandle(uint16_t handle)
	{
		Desc* table = Table();
		for (size_t i = 0; i < kCount; i++)
		{
			if (table[i].handle == handle)
				return &table[i];
		}
		return nullptr;
	}

	inline std::string Describe(const Desc* desc)
	{
		if (desc == nullptr)
			return " [unknown]";

		return fmt::format(" [{} {}x{} fmt {}]",
			IndexOf(desc), desc->width, desc->height, desc->format);
	}

	inline std::string Describe(uint16_t handle)
	{
		const Desc* desc = FromHandle(handle);
		if (desc == nullptr)
			return fmt::format(" [handle {} unknown]", handle);

		return Describe(desc);
	}
}
