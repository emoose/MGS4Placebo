#pragma once

// This header handles bgfx related data in the game, pulling in bgfx headers, 
// confirming sizes match what MGS4 uses, and providing accessors to the bgfx
// data.
// 
// TODO: currently all tied to offsets from first version of game, use
// ModUtils Patterns to scan for accesses to them instead.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "bgfx/internal.hpp"
#include "plugin.hpp"

namespace BgfxGame
{
	inline constexpr uint32_t kMaxViews = BGFX_CONFIG_MAX_VIEWS;

	// Game memory addresses (exe-relative)
	namespace Offsets
	{
		// bgfx::s_ctx, a pointer to the single Context the game creates.
		constexpr uintptr_t ContextPtr = 0x23D37220;

		// bgfx::g_caps.
		constexpr uintptr_t Caps = 0x23D37090;

		// bgfx::Context::frame(bool), called once per presented frame.
		constexpr uintptr_t ContextFrame = 0x761730;

		// bgfx::Context::setView*, the only writers of the view table.
		constexpr uintptr_t ContextSetViewFrameBuffer = 0x76BFF0;
		constexpr uintptr_t ContextSetViewRect = 0x76C2D0;
		constexpr uintptr_t ContextSetViewClear = 0x76BE60;

		// bgfx::isFrameBufferValid and bgfx::isTextureValid, which say why the
		// renderer turned a resource down.
		constexpr uintptr_t IsFrameBufferValid = 0x7646A0;
		constexpr uintptr_t IsTextureValid = 0x764D10;

		// bgfx_interface_vtbl_t: 194 function pointers in IDL order. Calling
		// through it is steadier than hardcoding each function's address.
		constexpr uintptr_t InterfaceVtbl = 0x1C548E0;
	}

	// Slots in the interface vtable, in the order bgfx's IDL declares them.
	namespace ApiSlot
	{
		constexpr uint32_t get_caps = 21;
		constexpr uint32_t create_shader = 57;
		constexpr uint32_t create_program = 61;
		constexpr uint32_t create_compute_program = 62;
		constexpr uint32_t create_texture_2d = 68;
		constexpr uint32_t destroy_texture = 78;
		constexpr uint32_t create_frame_buffer_from_handles = 81;
		constexpr uint32_t create_uniform = 87;
		constexpr uint32_t set_view_rect = 96;
		constexpr uint32_t set_view_frame_buffer = 102;
		constexpr uint32_t set_state = 155;
		constexpr uint32_t set_uniform = 163;
		constexpr uint32_t set_texture = 178;
		constexpr uint32_t dispatch = 190;
		constexpr uint32_t set_image = 189;
	}

	// Null until bgfx has been initialised, which happens well after this DLL
	// loads, so every caller has to handle the null.
	inline bgfx::Context* Context()
	{
		auto* slot = Module::exe_ptr<bgfx::Context*>(Offsets::ContextPtr);
		return slot != nullptr ? *slot : nullptr;
	}

	inline bgfx::View* Views()
	{
		bgfx::Context* ctx = Context();
		return ctx != nullptr ? ctx->m_view : nullptr;
	}

	// bgfx submits views in this order rather than by view id.
	inline uint16_t* ViewRemap()
	{
		bgfx::Context* ctx = Context();
		return ctx != nullptr ? ctx->m_viewRemap : nullptr;
	}

	// bgfx keeps resource names as bx::StringT, which is not always zero
	// terminated, so the length has to come from the string itself.
	inline std::string_view StringOf(const bx::StringT& str)
	{
		if (str.m_ptr == nullptr || str.m_len <= 0)
			return {};
		return std::string_view(str.m_ptr, size_t(str.m_len));
	}

	// The framebuffer a view draws to, or null for the back buffer.
	inline const bgfx::FrameBufferRef* FrameBuffer(bgfx::FrameBufferHandle fbh)
	{
		bgfx::Context* ctx = Context();
		if (ctx == nullptr || fbh.idx >= BGFX_CONFIG_MAX_FRAME_BUFFERS)
			return nullptr;
		return &ctx->m_frameBufferRef[fbh.idx];
	}

	inline const bgfx::Caps* Caps()
	{
		return Module::exe_ptr<bgfx::Caps>(Offsets::Caps);
	}

	// The vtable is static data, so this is valid before bgfx initialises.
	inline void* ApiFunction(uint32_t slot)
	{
		auto* vtbl = Module::exe_ptr<void*>(Offsets::InterfaceVtbl);
		return vtbl != nullptr ? vtbl[slot] : nullptr;
	}

	inline std::string ErrorMessage(const bx::Error* error)
	{
		if (error == nullptr || error->message == nullptr || error->length <= 0)
			return "no message";
		return std::string(error->message, size_t(error->length));
	}
}

// Pin bgfx struct sizes to what the game uses, our bgfx headers have most
// of the games customizations added, if those change these should error.
static_assert(sizeof(bgfx::Context) == 79768768);
static_assert(offsetof(bgfx::Context, m_render) == 77623936);
static_assert(offsetof(bgfx::Context, m_viewRemap) == 79664520);
static_assert(offsetof(bgfx::Context, m_view) == 79667648);
static_assert(offsetof(bgfx::Context, m_init) == 79766216);

static_assert(sizeof(bgfx::View) == 192);
static_assert(offsetof(bgfx::View, m_rect) == 16);
static_assert(offsetof(bgfx::View, m_fbh) == 160);

// Widened by the fork's extra member, which also moves everything after
// m_bind in EncoderImpl.
static_assert(sizeof(bgfx::Binding) == 24);
static_assert(sizeof(bgfx::RenderBind) == 384);
static_assert(sizeof(bgfx::EncoderImpl) == 768);
static_assert(offsetof(bgfx::EncoderImpl, m_bind) == 256);
static_assert(offsetof(bgfx::EncoderImpl, m_numVertices) == 656);

// Untouched by the fork.
static_assert(sizeof(bgfx::RenderDraw) == 128);
static_assert(sizeof(bgfx::RenderItem) == 128);
static_assert(offsetof(bgfx::RenderDraw, m_instanceDataOffset) == 76);

// Narrowed by the fork, which drops m_numVertices from it.
static_assert(sizeof(bgfx::DynamicVertexBuffer) == 24);
static_assert(offsetof(bgfx::DynamicVertexBuffer, m_stride) == 16);
static_assert(sizeof(bgfx::DynamicIndexBuffer) == 20);

static_assert(sizeof(bgfx::ShaderRef) == 48);
static_assert(sizeof(bgfx::TextureRef) == 64);
static_assert(sizeof(bgfx::ProgramRef) == 6);

// From the vendored public header, checked against the game's own tables.
static_assert(sizeof(bgfx::Caps) == 336);
static_assert(offsetof(bgfx::Caps, limits) == 40);
static_assert(offsetof(bgfx::Caps, formats) == 136);
static_assert(sizeof(bgfx::Attachment) == 16);
static_assert(sizeof(bgfx::VertexLayout) == 80);
static_assert(bgfx::RendererType::Count == 13);
static_assert(bgfx::TextureFormat::RGBA8 == 67);
static_assert(bgfx::TextureFormat::D24S8 == 90);
