#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_rendertargets.hpp"

#include <string>

namespace Settings
{
	Setting<int> MsaaSamples("Rendering", "MSAA", 0,
		"Multisampling for the main scene render targets. 0 leaves the game as "
		"it is.",
		Settings::Range<int>{ 0, 16 });

	Setting<bool> MsaaForceRasterizer("Rendering", "MSAAForceRasterizer", true,
		"Turns on multisample rasterization for every draw. Without it the "
		"hardware takes one sample per pixel whatever the render target holds, "
		"so multisampled targets resolve to the same aliased image.");

	Setting<bool> MsaaLogCreateResults("Rendering", "MSAALogCreateResults", false,
		"Reports why the renderer rejects a framebuffer. The release build drops "
		"those messages, so a rejected framebuffer otherwise shows up only as a "
		"black screen.");

	Setting<bool> MsaaLogFrameBuffers("Rendering", "MSAALogFrameBuffers", false,
		"Logs which render targets get attached to each framebuffer, to show "
		"which ones have to share a sample count.");

	Setting<bool> MsaaLogTargets("Rendering", "MSAALogTargets", false,
		"Logs every render target as it is created, with the flags it ends up "
		"with.");
};

// The engine keeps a table of render target descriptors and creates the bgfx
// texture for one in a single place, building the 64-bit texture flags from its
// own usage bits:
//
//   and  r10d, 70h / add r10, r10 / or r10, rax / shl r10, 7
//   and  eax, 4                        ; the "is a render target" bit
//   or   r10, rax
//   shl  r10, 22h                      ; << 34, producing the bgfx flags
//
// bgfx holds the render target mode as a 3-bit field at bit 36: 1 is a plain
// render target, 2 to 5 select MSAA x2 through x16. The shift by 34 puts that
// field at bits 2 to 4 of the value before the shift, so the mode can be
// changed there with no 64-bit constant and no change to the instruction that
// follows.
namespace
{
	// Flags the MSAA hook last applied, so a texture the renderer turns down can
	// be reported with the flags that caused it. The two run back to back.
	uint64_t g_lastFlags = 0;
}

class MsaaHook : public Hook
{
	inline static SafetyHookMid TextureFlags_hook = {};

	static void TextureFlags_dest(safetyhook::Context& ctx)
	{
		const uint32_t field = RenderTargets::RtFieldForSamples(Settings::MsaaSamples);
		if (field == 0)
			return;

		// Only touch textures the game is about to create as a plain render
		// target. Everything else keeps the flags it asked for.
		if (((ctx.r10 & RenderTargets::kRtFieldMask) >> RenderTargets::kRtFieldShift) != RenderTargets::kRtPlain)
			return;

		const RenderTargets::Desc* desc = reinterpret_cast<RenderTargets::Desc*>(ctx.rbx);

		// Every attachment of a framebuffer has to agree on its sample count,
		// and the renderer separately requires them all to be the same size. So
		// deciding this purely on size makes the attachments of any one
		// framebuffer agree by construction, whatever they turn out to be.
		//
		// Selecting particular targets instead cannot hold that: a target is
		// named by its slot in the engine's table, the engine hands out the
		// lowest free slot, and which slot a target lands in shifts with load
		// order. A list gathered from one run picks different targets the next,
		// leaving a framebuffer with one multisampled attachment and one not.
		//
		// Shadow and post-process targets are created at their own sizes, so
		// anything below the render size is left alone.
		const bool selected = (desc->width == RenderTargets::RenderWidth() && desc->height == RenderTargets::RenderHeight());

		if (selected)
		{
			ctx.r10 = (ctx.r10 & ~uint64_t(RenderTargets::kRtFieldMask)) | (uint64_t(field) << RenderTargets::kRtFieldShift);

			if (RenderTargets::IsDepth(desc))
			{
				ctx.r10 |= RenderTargets::kMsaaSampleBit;
			}
		}

		g_lastFlags = uint64_t(ctx.r10) << 34;

		if (Settings::MsaaLogTargets)
		{
			// The flags are logged as the renderer sees them, after the shift
			// the engine applies, so they can be read against its own
			// constants: the render target mode sits at bits 36 to 38, and the
			// sample count every attachment of a framebuffer has to agree on is
			// derived from it.
			spdlog::debug("MsaaHook: target {} {}x{} format {}{} usage {:#x}{} flags {:#x}",
				RenderTargets::IndexOf(desc), desc->width, desc->height, desc->format,
				RenderTargets::IsDepth(desc) ? " (depth)" : "",
				desc->usage,
				selected ? fmt::format(" -> MSAA x{}", int(Settings::MsaaSamples)) : "",
				g_lastFlags);
		}
	}

public:
	std::string_view description() override
	{
		return "MsaaHook";
	}

	bool validate() override
	{
		return RenderTargets::RtFieldForSamples(Settings::MsaaSamples) != 0;
	}

	void declare_settings() override
	{
		// Render targets are created as the engine starts up, so a change only
		// reaches them on the next run.
		Settings::MsaaSamples.needs_restart();
		Settings::MsaaLogTargets.needs_restart();
	}

	bool apply() override
	{
		//   shl  r10, 22h
		//   test dl, 8
		if (!Module::code_matches(0x677E61, { 0x49, 0xC1, 0xE2, 0x22, 0xF6, 0xC2, 0x08 }))
			return false;

		TextureFlags_hook = safetyhook::create_mid(
			Module::exe_ptr(0x677E61), TextureFlags_dest);

		if (!TextureFlags_hook)
			return false;

		spdlog::info("MsaaHook: requesting MSAA x{} on full size render targets",
			int(Settings::MsaaSamples));

		return true;
	}

	static MsaaHook instance;
};
MsaaHook MsaaHook::instance;

// Every framebuffer is built from textures the engine created earlier, and
// Direct3D requires all of a framebuffer's attachments to have the same sample
// count. This reports which targets end up together so the ones that have to be
// promoted as a set can be seen.
class MsaaFrameBufferLogHook : public Hook
{
	inline static SafetyHookMid Assemble_hook = {};
	inline static SafetyHookMid AssembleFromDescs_hook = {};

	// The path that takes descriptor pointers rather than texture handles, and
	// is the one the scene pass goes through: up to four colour attachments and
	// a depth attachment.
	static void AssembleFromDescs_dest(safetyhook::Context& ctx)
	{
		const RenderTargets::Desc* const* descs = reinterpret_cast<RenderTargets::Desc**>(ctx.rcx);
		const uint8_t count = uint8_t(ctx.rdx);
		if (descs == nullptr || count == 0)
			return;

		std::string line;
		for (uint8_t i = 0; i < count; i++)
		{
			const RenderTargets::Desc* desc = descs[i];
			if (!RenderTargets::InTable(desc))
			{
				line += " [out of table]";
				continue;
			}

			line += RenderTargets::Describe(desc);
		}

		spdlog::debug("MsaaHook: scene framebuffer of {} ->{}", count, line);
	}

	static void Assemble_dest(safetyhook::Context& ctx)
	{
		const uint16_t* handles = reinterpret_cast<uint16_t*>(ctx.rcx);
		const uint8_t count = uint8_t(ctx.rdx);
		if (handles == nullptr || count == 0)
			return;

		std::string line;
		for (uint8_t i = 0; i < count; i++)
		{
			line += RenderTargets::Describe(handles[i]);
		}

		spdlog::debug("MsaaHook: framebuffer of {} ->{}", count, line);
	}

public:
	std::string_view description() override
	{
		return "MsaaFrameBufferLogHook";
	}

	bool validate() override
	{
		return Settings::MsaaLogFrameBuffers;
	}

	void declare_settings() override
	{
		Settings::MsaaLogFrameBuffers.needs_restart();
	}

	bool apply() override
	{
		//   push rbp / push rbx / push rsi / push r14 / mov rbp, rsp
		if (!Module::code_matches(0x6621C0, { 0x40, 0x55, 0x53, 0x56, 0x41, 0x56, 0x48, 0x8B, 0xEC }))
			return false;

		//   push r14 / sub rsp, 60h / mov [rsp+58h], rbx
		if (!Module::code_matches(0x662440, { 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x89, 0x5C, 0x24, 0x58 }))
			return false;

		Assemble_hook = safetyhook::create_mid(
			Module::exe_ptr(0x6621C0), Assemble_dest);

		AssembleFromDescs_hook = safetyhook::create_mid(
			Module::exe_ptr(0x662440), AssembleFromDescs_dest);

		return bool(Assemble_hook) && bool(AssembleFromDescs_hook);
	}

	static MsaaFrameBufferLogHook instance;
};
MsaaFrameBufferLogHook MsaaFrameBufferLogHook::instance;

// bgfx takes Direct3D's MultisampleEnable from BGFX_STATE_MSAA, a per-draw
// state bit the game never sets, so its triangles are rasterized with a single
// sample per pixel no matter how many samples the render target has. The
// rasterizer state cache reads that bit in one place:
//
//   shr rdx, 38h        ; state >> 56, BGFX_STATE_MSAA
//   and edx, 1
//   mov [rbp+var_10], edx   ; D3D11_RASTERIZER_DESC::MultisampleEnable
//
// Replacing the two instructions that extract it with a constant 1 enables it
// for every draw. On a render target that holds one sample Direct3D ignores it,
// so the targets left alone keep rendering as they did.
class MsaaRasterizerHook : public Hook
{
public:
	std::string_view description() override
	{
		return "MsaaRasterizerHook";
	}

	bool validate() override
	{
		return Settings::MsaaForceRasterizer && RenderTargets::RtFieldForSamples(Settings::MsaaSamples) != 0;
	}

	void declare_settings() override
	{
		Settings::MsaaForceRasterizer.needs_restart();
	}

	bool apply() override
	{
		//   shr rdx, 38h
		//   and edx, 1
		if (!Module::code_matches(0x792AA1, { 0x48, 0xC1, 0xEA, 0x38, 0x83, 0xE2, 0x01 }))
			return false;

		//   mov edx, 1  followed by two nops, keeping the length the same
		Memory::VP::Patch(Module::exe_ptr(0x792AA1), { 0xBA, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90 });

		return true;
	}

	static MsaaRasterizerHook instance;
};
MsaaRasterizerHook MsaaRasterizerHook::instance;

// bgfx refuses a framebuffer whose depth attachment is multisampled unless it is
// write only, because it cannot resolve one: Direct3D has no resolve for depth
// formats. Reading the target as multisampled instead needs no resolve, so the
// check is too strict for that case, and it is the only thing standing in the
// way of a readable multisampled depth buffer.
//
//   bt rcx, 27h        ; the write only flag
//   jb  ok             ; accepted
//   ...                ; otherwise the framebuffer is rejected
//
// Turning that branch into an unconditional one accepts a multisampled depth
// attachment however it was flagged. The rest of the validation is untouched.
class MsaaDepthValidationHook : public Hook
{
public:
	std::string_view description() override
	{
		return "MsaaDepthValidationHook";
	}

	bool validate() override
	{
		return RenderTargets::RtFieldForSamples(Settings::MsaaSamples) != 0;
	}

	bool apply() override
	{
		//   bt rcx, 27h
		//   jb  short +0Ch
		if (!Module::code_matches(0x7648F1, { 0x48, 0x0F, 0xBA, 0xE1, 0x27, 0x72, 0x0C }))
			return false;

		// The renderer will not create a texture asking to be read as
		// multisampled unless it recorded that the format supports it, and it
		// records nothing of the sort for the depth formats:
		//
		//   bt   r8, 23h        ; the multisampled read flag
		//   mov  edx, 4000h     ; the recorded support bit
		//   and  ax, dx
		//   cmp  r12w, ax
		//   jnz  ok             ; supported
		//
		// What it recorded comes from asking the driver about a depth format
		// directly, which reports far less than the view a shader would actually
		// read it through, so the answer is not the one that matters here. The
		// branch is turned into an unconditional one to find out what the
		// hardware does rather than what was recorded about it.
		if (!Module::code_matches(0x764F0B, { 0xBA, 0x00, 0x40, 0x00, 0x00, 0x66, 0x23, 0xC2,
		                                      0x66, 0x44, 0x3B, 0xE0, 0x75, 0x09 }))
			return false;

		// jb -> jmp, keeping the same displacement and length.
		Memory::VP::Patch<uint8_t>(Module::exe_ptr(0x7648F6), 0xEB);

		// jnz -> jmp, likewise.
		Memory::VP::Patch<uint8_t>(Module::exe_ptr(0x764F17), 0xEB);

		return true;
	}

	static MsaaDepthValidationHook instance;
};
MsaaDepthValidationHook MsaaDepthValidationHook::instance;


// All three texture creation paths meet at the instruction that stores the
// handle the renderer handed back, with the descriptor still in rbx. A failed
// creation returns the invalid handle rather than reporting anything, and the
// release build says nothing, so a texture that never came into being shows up
// only as whatever breaks later: a framebuffer missing its depth attachment
// renders with no depth test at all, and walls stop hiding what is behind them.
//
// Reported here rather than alongside the other renderer errors because the
// flags worth naming are the ones the MSAA hook applied a moment earlier.
class MsaaTextureResultHook : public Hook
{
	inline static SafetyHookMid Result_hook = {};

	static void Result_dest(safetyhook::Context& ctx)
	{
		const uint16_t handle = uint16_t(ctx.rax);
		if (handle != UINT16_MAX)
			return;

		const RenderTargets::Desc* desc = reinterpret_cast<RenderTargets::Desc*>(ctx.rbx);
		spdlog::error("MsaaHook: texture {} {}x{} format {} was refused, flags {:#x}",
			RenderTargets::IndexOf(desc), desc->width, desc->height, desc->format, g_lastFlags);
	}

public:
	std::string_view description() override
	{
		return "MsaaTextureResultHook";
	}

	bool validate() override
	{
		return Settings::MsaaLogCreateResults;
	}

	bool apply() override
	{
		//   mov [rbx+34h], ax
		//   and dword ptr [rbx+20h], 0FFFFFFFEh
		if (!Module::code_matches(0x677EE5, { 0x66, 0x89, 0x43, 0x34, 0x83, 0x63, 0x20, 0xFE }))
			return false;

		Result_hook = safetyhook::create_mid(
			Module::exe_ptr(0x677EE5), Result_dest);

		return bool(Result_hook);
	}

	static MsaaTextureResultHook instance;
};
MsaaTextureResultHook MsaaTextureResultHook::instance;
