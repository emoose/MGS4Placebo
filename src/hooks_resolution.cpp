#include "hook_mgr.hpp"
#include "plugin.hpp"

namespace Settings
{
	Setting<bool> ResolutionFixEnable("Resolution", "Enable", false);

	Setting<bool> ResolutionFixReticle("Resolution", "FixReticle", true,
		"Keeps the aiming reticle on screen when rendering wider or taller than "
		"4096 pixels. Below that it changes nothing.");

	Setting<bool> ResolutionUnlockLimits("Resolution", "UnlockLimits", true,
		"Removes the 5760x2160 ceiling and the clamp that caps the game to the "
		"size the monitor reports, so a larger resolution set in the game's own "
		"settings is used as asked for.");

	Setting<int> ResolutionBufferWidth("Resolution", "BufferWidth", 0,
		"Width the game renders at, independent of the window. 0 keeps whatever "
		"the game worked out for itself.",
		Settings::Range<int>{ 0, 16384 });

	Setting<int> ResolutionBufferHeight("Resolution", "BufferHeight", 0,
		"Height the game renders at, independent of the window. 0 keeps whatever "
		"the game worked out for itself.",
		Settings::Range<int>{ 0, 16384 });
};

// render.bufferSizeX and render.bufferSizeY in mgs4.ini are read into globals
// and then overwritten a few calls later, so editing them does nothing. The
// values that survive are worked out by the size resolver from the window size
// alone, and clamped twice on the way: to 5760x2160, and then to the size the
// monitor reports. These hooks work on the resolver instead of the ini keys.
namespace
{
	// Fields of the resolved display state, all set by the size resolver.
	// Render target size, which the whole engine reads back through its getters.
	int32_t* BufferWidth() { return Module::exe_ptr<int32_t>(0x3BD1178); }
	int32_t* BufferHeight() { return Module::exe_ptr<int32_t>(0x3BD117C); }

	// Area the rendered image is placed in, and where that area starts. The HUD
	// lays itself out on a 16x32 grid over this rather than over the render
	// target, so it has to follow the render target size.
	int32_t* ViewportWidth() { return Module::exe_ptr<int32_t>(0x3BD1168); }
	int32_t* ViewportHeight() { return Module::exe_ptr<int32_t>(0x3BD116C); }
	int32_t* ViewportX() { return Module::exe_ptr<int32_t>(0x3BD1170); }
	int32_t* ViewportY() { return Module::exe_ptr<int32_t>(0x3BD1174); }

	// Highest value the size resolver will compare against, in place of the
	// 5760 and 2160 it ships with. Signed comparison, so nothing exceeds it.
	constexpr int32_t kUnlockedLimit = 0x7FFFFFFF;
}

class ResolutionUnlockHook : public Hook
{
public:
	std::string_view description() override
	{
		return "ResolutionUnlockHook";
	}

	bool validate() override
	{
		return Settings::ResolutionFixEnable && Settings::ResolutionUnlockLimits;
	}

	void declare_settings() override
	{
		// The resolution is resolved once during startup, so none of this can
		// be picked up while the game runs.
		Settings::ResolutionFixEnable.needs_restart();
		Settings::ResolutionUnlockLimits.needs_restart();
		Settings::ResolutionBufferWidth.needs_restart();
		Settings::ResolutionBufferHeight.needs_restart();
	}

	bool apply() override
	{
		// Upper ceiling in the display mode resolver: anything wider than 5760
		// or taller than 2160 sends it back through the monitor's mode list
		// looking for something smaller. Raising both bounds means the request is
		// simply kept. The second pair is the same test inside that search, which
		// nothing reaches once the first pair stops triggering it.
		const bool limitsMatch =
			Module::code_matches(0x65DE3D, { 0x81, 0xFA, 0x80, 0x16, 0x00, 0x00 })   // cmp edx, 1680h
			&& Module::code_matches(0x65DE45, { 0x3D, 0x70, 0x08, 0x00, 0x00 })      // cmp eax, 870h
			&& Module::code_matches(0x65DEB4, { 0x3D, 0x80, 0x16, 0x00, 0x00 })      // cmp eax, 1680h
			&& Module::code_matches(0x65DEBB, { 0x81, 0x39, 0x70, 0x08, 0x00, 0x00 });// cmp [rcx], 870h

		// Clamps the requested size down to the monitor's own width and height,
		// one pair for exclusive fullscreen and one for windowed. Removing them
		// lets a larger request through.
		const bool clampsMatch =
			Module::code_matches(0x65E27C, { 0x0F, 0x4F, 0x5D, 0x28 })      // cmovg ebx, [rbp+monitorWidth]
			&& Module::code_matches(0x65E28B, { 0x0F, 0x4F, 0x7D, 0x30 })   // cmovg edi, [rbp+monitorHeight]
			&& Module::code_matches(0x65E2DD, { 0x0F, 0x4F, 0x5D, 0x28 })
			&& Module::code_matches(0x65E2E4, { 0x0F, 0x4F, 0x7D, 0x30 });

		if (!limitsMatch || !clampsMatch)
			return false;

		// Offsets of the immediate within each compare, past the opcode and modrm.
		Memory::VP::Patch<int32_t>(Module::exe_ptr(0x65DE3F), kUnlockedLimit);
		Memory::VP::Patch<int32_t>(Module::exe_ptr(0x65DE46), kUnlockedLimit);
		Memory::VP::Patch<int32_t>(Module::exe_ptr(0x65DEB5), kUnlockedLimit);
		Memory::VP::Patch<int32_t>(Module::exe_ptr(0x65DEBD), kUnlockedLimit);

		Memory::VP::Nop(Module::exe_ptr(0x65E27C), 4);
		Memory::VP::Nop(Module::exe_ptr(0x65E28B), 4);
		Memory::VP::Nop(Module::exe_ptr(0x65E2DD), 4);
		Memory::VP::Nop(Module::exe_ptr(0x65E2E4), 4);

		return true;
	}

	static ResolutionUnlockHook instance;
};
ResolutionUnlockHook ResolutionUnlockHook::instance;

class ResolutionBufferHook : public Hook
{
	inline static SafetyHookInline ResolveSizes_hook = {};

	// The size resolver derives the render target from the window size, forces
	// it to 16:9, and clamps it, so render.bufferSizeX and render.bufferSizeY
	// in mgs4.ini never reach it. Letting it run first and rewriting the result
	// covers all three display modes and both of its callers, including the one
	// that re-runs when display settings change while the game is open.
	static void ResolveSizes_dest()
	{
		ResolveSizes_hook.call<void>();

		const int32_t settingWidth = Settings::ResolutionBufferWidth;
		const int32_t settingHeight = Settings::ResolutionBufferHeight;

		const int32_t width = settingWidth > 0 ? settingWidth : *BufferWidth();
		const int32_t height = settingHeight > 0 ? settingHeight : *BufferHeight();

		*BufferWidth() = width;
		*BufferHeight() = height;

		// Without this the viewport keeps the size the resolver worked out for
		// the window, because it only follows the render target while that is
		// no larger than the window. Everything the HUD positions, the
		// crosshair included, would then be laid out for the smaller area.
		*ViewportWidth() = width;
		*ViewportHeight() = height;
		*ViewportX() = 0;
		*ViewportY() = 0;

		// The resolver runs again whenever display settings change, so this is
		// reported once rather than on every pass.
		static bool reported = false;
		if (!reported)
		{
			reported = true;
			spdlog::info("ResolutionBufferHook: rendering at {}x{}", width, height);
		}
	}

public:
	std::string_view description() override
	{
		return "ResolutionBufferHook";
	}

	bool validate() override
	{
		return Settings::ResolutionFixEnable
			&& (Settings::ResolutionBufferWidth > 0 || Settings::ResolutionBufferHeight > 0);
	}

	bool apply() override
	{
		//   push rbp / push rbx / push rsi / push rdi / mov rbp, rsp
		if (!Module::code_matches(0x65E220, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8B, 0xEC }))
			return false;

		ResolveSizes_hook = safetyhook::create_inline(
			Module::exe_ptr(0x65E220), ResolveSizes_dest);

		if (!ResolveSizes_hook)
			return false;

		return true;
	}

	static ResolutionBufferHook instance;
};
ResolutionBufferHook ResolutionBufferHook::instance;

// Based on https://github.com/drbermejor/mgs4Ultra120 reticle fix code
class ReticleTruncationHook : public Hook
{
public:
	std::string_view description() override
	{
		return "ReticleTruncationHook";
	}

	bool validate() override
	{
		return Settings::ResolutionFixReticle;
	}

	void declare_settings() override
	{
		Settings::ResolutionFixReticle.needs_restart();
	}

	bool apply() override
	{
		// The reticle's screen position reaches the UI canvas as a fixed point
		// value in sixteenths of a pixel, and each of the four conversions to
		// canvas space keeps only the low 16 bits of it:
		//
		//   cvttss2si ecx, xmm0            ; ecx = screen_x * 16
		//   movsx     edx, cx              ; drops everything above bit 15
		//   lea       eax, [rdx+rdx*4]
		//   shl       eax, 8               ; * 1280, the canvas width
		//   cdq
		//   idiv      cs:dword_141B00000   ; / render width
		//
		// A centred reticle holds half the render size, so the value passes
		// 32767 once that reaches 4096 pixels: 2048 * 16 = 32768. At 5120 wide
		// it wraps to -24576 and the reticle is placed at -6144 on the canvas,
		// well off the left edge, which reads as the reticle having vanished.
		// Widening each move to 32 bits keeps the real value. Each replacement
		// is a byte shorter than what it replaces, so it ends in a nop and no
		// following instruction has to move.
		const bool matches =
			Module::code_matches(0xE39816, { 0x0F, 0xBF, 0xD1 })          // movsx edx, cx    (X)
			&& Module::code_matches(0xE39830, { 0x44, 0x0F, 0xBF, 0xC1 }) // movsx r8d, cx    (Y)
			&& Module::code_matches(0xE398F1, { 0x0F, 0xBF, 0xD1 })       // movsx edx, cx    (Y)
			&& Module::code_matches(0xE3990C, { 0x0F, 0xBF, 0xD1 });      // movsx edx, cx    (X)

		if (!matches)
			return false;

		Memory::VP::Patch(Module::exe_ptr(0xE39816), { 0x8B, 0xD1, 0x90 });        // mov edx, ecx
		Memory::VP::Patch(Module::exe_ptr(0xE39830), { 0x41, 0x89, 0xC8, 0x90 });  // mov r8d, ecx
		Memory::VP::Patch(Module::exe_ptr(0xE398F1), { 0x8B, 0xD1, 0x90 });        // mov edx, ecx
		Memory::VP::Patch(Module::exe_ptr(0xE3990C), { 0x8B, 0xD1, 0x90 });        // mov edx, ecx

		return true;
	}

	static ReticleTruncationHook instance;
};
ReticleTruncationHook ReticleTruncationHook::instance;
