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

		// bgfx::Context::init(const Init&). Calls frame() several times while it
		// is still building the renderer, so nothing added is safe until it returns.
		constexpr uintptr_t ContextInit = 0x763DD0;

		// bgfx::Context::frame(bool), called once per presented frame.
		constexpr uintptr_t ContextFrame = 0x761730;

		// bgfx::VertexLayout::VertexLayout(). Declared by the vendored header but
		// implemented in bgfx's vertexlayout.cpp, which is not built here, so
		// stubs.cpp forwards to the game's copy rather than inventing one.
		constexpr uintptr_t VertexLayoutCtor = 0x774EB0;

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
		constexpr uint32_t vertex_layout_begin = 1;
		constexpr uint32_t vertex_layout_add = 2;
		constexpr uint32_t vertex_layout_end = 6;
		constexpr uint32_t alloc = 23;
		constexpr uint32_t copy = 24;
		constexpr uint32_t get_avail_transient_vertex_buffer = 49;
		constexpr uint32_t alloc_transient_vertex_buffer = 52;
		constexpr uint32_t get_caps = 21;
		constexpr uint32_t create_shader = 57;
		constexpr uint32_t create_program = 61;
		constexpr uint32_t create_compute_program = 62;
		constexpr uint32_t create_texture_2d = 68;
		constexpr uint32_t get_texture = 85;
		constexpr uint32_t create_frame_buffer = 79;
		constexpr uint32_t destroy_texture = 78;
		constexpr uint32_t destroy_shader = 60;
		constexpr uint32_t destroy_program = 63;
		constexpr uint32_t destroy_frame_buffer = 86;
		constexpr uint32_t destroy_uniform = 89;
		constexpr uint32_t create_frame_buffer_from_handles = 81;
		constexpr uint32_t create_uniform = 87;
		constexpr uint32_t set_view_name = 95;
		constexpr uint32_t set_view_rect = 96;
		constexpr uint32_t set_view_rect_ratio = 97;
		constexpr uint32_t set_view_clear = 99;
		constexpr uint32_t set_view_frame_buffer = 102;
		constexpr uint32_t set_view_transform = 103;
		constexpr uint32_t set_view_order = 104;
		constexpr uint32_t touch = 179;
		constexpr uint32_t set_state = 155;
		constexpr uint32_t set_uniform = 163;
		constexpr uint32_t set_transient_vertex_buffer = 171;
		constexpr uint32_t set_texture = 178;
		constexpr uint32_t submit = 180;
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

	// The texture behind a handle, or null if the handle is out of range.
	inline const bgfx::TextureRef* Texture(bgfx::TextureHandle th)
	{
		bgfx::Context* ctx = Context();
		if (ctx == nullptr || th.idx >= BGFX_CONFIG_MAX_TEXTURES)
			return nullptr;
		return &ctx->m_textureRef[th.idx];
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

	// bgfx's C API, reached through the interface vtable rather than by
	// hardcoding each function's address. Signatures come from bgfx's IDL.
	namespace Api
	{
		using SetViewName = void (*)(uint16_t id, const char* name);
		using SetViewRect = void (*)(uint16_t id, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
		using SetViewRectRatio = void (*)(uint16_t id, uint16_t x, uint16_t y, bgfx::BackbufferRatio::Enum ratio);
		using SetViewClear = void (*)(uint16_t id, uint16_t flags, uint32_t rgba, float depth, uint8_t stencil);
		using SetViewFrameBuffer = void (*)(uint16_t id, bgfx::FrameBufferHandle handle);
		using SetViewOrder = void (*)(uint16_t id, uint16_t num, const uint16_t* order);
		using Touch = void (*)(uint16_t id);

		inline SetViewName setViewName() { return SetViewName(ApiFunction(ApiSlot::set_view_name)); }
		inline SetViewRect setViewRect() { return SetViewRect(ApiFunction(ApiSlot::set_view_rect)); }
		inline SetViewRectRatio setViewRectRatio() { return SetViewRectRatio(ApiFunction(ApiSlot::set_view_rect_ratio)); }
		inline SetViewClear setViewClear() { return SetViewClear(ApiFunction(ApiSlot::set_view_clear)); }
		inline SetViewFrameBuffer setViewFrameBuffer() { return SetViewFrameBuffer(ApiFunction(ApiSlot::set_view_frame_buffer)); }
		inline SetViewOrder setViewOrder() { return SetViewOrder(ApiFunction(ApiSlot::set_view_order)); }
		inline Touch touch() { return Touch(ApiFunction(ApiSlot::touch)); }

		using VertexLayoutBegin = bgfx::VertexLayout* (*)(bgfx::VertexLayout*, bgfx::RendererType::Enum);
		using VertexLayoutAdd = bgfx::VertexLayout* (*)(bgfx::VertexLayout*, bgfx::Attrib::Enum, uint8_t, bgfx::AttribType::Enum, bool, bool);
		using VertexLayoutEnd = void (*)(bgfx::VertexLayout*);
		using Copy = const bgfx::Memory* (*)(const void* data, uint32_t size);
		using CreateShader = bgfx::ShaderHandle (*)(const bgfx::Memory* mem);
		using CreateProgram = bgfx::ProgramHandle (*)(bgfx::ShaderHandle vsh, bgfx::ShaderHandle fsh, bool destroyShaders);
		using CreateUniform = bgfx::UniformHandle (*)(const char* name, bgfx::UniformType::Enum type, uint16_t num);
		using CreateFrameBuffer = bgfx::FrameBufferHandle (*)(uint16_t w, uint16_t h, bgfx::TextureFormat::Enum fmt, uint64_t textureFlags);
		using GetAvailTransientVertexBuffer = uint32_t (*)(uint32_t num, const bgfx::VertexLayout* layout);
		using AllocTransientVertexBuffer = void (*)(bgfx::TransientVertexBuffer* tvb, uint32_t num, const bgfx::VertexLayout* layout);
		using SetTransientVertexBuffer = void (*)(uint8_t stream, const bgfx::TransientVertexBuffer* tvb, uint32_t startVertex, uint32_t numVertices);
		using SetViewTransform = void (*)(uint16_t id, const void* view, const void* proj);
		using SetState = void (*)(uint64_t state, uint32_t rgba);
		using SetTexture = void (*)(uint8_t stage, bgfx::UniformHandle sampler, bgfx::TextureHandle handle, uint32_t flags);
		using SetUniform = void (*)(bgfx::UniformHandle handle, const void* value, uint16_t num);
		using Submit = void (*)(uint16_t id, bgfx::ProgramHandle program, uint32_t depth, uint8_t flags);
		using GetTexture = bgfx::TextureHandle (*)(bgfx::FrameBufferHandle handle, uint8_t attachment);

		inline VertexLayoutBegin vertexLayoutBegin() { return VertexLayoutBegin(ApiFunction(ApiSlot::vertex_layout_begin)); }
		inline VertexLayoutAdd vertexLayoutAdd() { return VertexLayoutAdd(ApiFunction(ApiSlot::vertex_layout_add)); }
		inline VertexLayoutEnd vertexLayoutEnd() { return VertexLayoutEnd(ApiFunction(ApiSlot::vertex_layout_end)); }
		inline Copy copy() { return Copy(ApiFunction(ApiSlot::copy)); }
		inline CreateShader createShader() { return CreateShader(ApiFunction(ApiSlot::create_shader)); }
		inline CreateProgram createProgram() { return CreateProgram(ApiFunction(ApiSlot::create_program)); }
		inline CreateUniform createUniform() { return CreateUniform(ApiFunction(ApiSlot::create_uniform)); }
		inline CreateFrameBuffer createFrameBuffer() { return CreateFrameBuffer(ApiFunction(ApiSlot::create_frame_buffer)); }
		inline GetAvailTransientVertexBuffer getAvailTransientVertexBuffer() { return GetAvailTransientVertexBuffer(ApiFunction(ApiSlot::get_avail_transient_vertex_buffer)); }
		inline AllocTransientVertexBuffer allocTransientVertexBuffer() { return AllocTransientVertexBuffer(ApiFunction(ApiSlot::alloc_transient_vertex_buffer)); }
		inline SetTransientVertexBuffer setTransientVertexBuffer() { return SetTransientVertexBuffer(ApiFunction(ApiSlot::set_transient_vertex_buffer)); }
		inline SetViewTransform setViewTransform() { return SetViewTransform(ApiFunction(ApiSlot::set_view_transform)); }
		inline SetState setState() { return SetState(ApiFunction(ApiSlot::set_state)); }
		inline SetTexture setTexture() { return SetTexture(ApiFunction(ApiSlot::set_texture)); }
		inline SetUniform setUniform() { return SetUniform(ApiFunction(ApiSlot::set_uniform)); }
		inline Submit submit() { return Submit(ApiFunction(ApiSlot::submit)); }
		inline GetTexture getTexture() { return GetTexture(ApiFunction(ApiSlot::get_texture)); }

	}

	// The engine's own shader uniform values, which it refreshes each frame.
	// Found by pairing each uniform handle in its registration function with the
	// buffer the matching setUniform call reads from.
	namespace GameUniform
	{
		inline const float* ProjectionViewWorld() { return Module::exe_ptr<float>(0x23BE68A0); } // float[16]
		inline const float* ProjectionView() { return Module::exe_ptr<float>(0x23BE68E0); }      // float[16]
		inline const float* Projection() { return Module::exe_ptr<float>(0x23BE6920); }          // float[16]
		inline const float* World() { return Module::exe_ptr<float>(0x23BE69A0); }               // float[16]
		inline const float* View() { return Module::exe_ptr<float>(0x23BE6960); }       // float[16]
		inline const float* EyePos() { return Module::exe_ptr<float>(0x23BE71F0); }     // float[4]
		inline const float* LightDir0() { return Module::exe_ptr<float>(0x23BE7210); }  // float[4]
		inline const float* LightColor0() { return Module::exe_ptr<float>(0x23BE7240); }
		inline const float* DynResUV() { return Module::exe_ptr<float>(0x23BE7340); }
		inline const float* DynResFactor() { return Module::exe_ptr<float>(0x23BE7350); }
	}

	inline std::string ErrorMessage(const bx::Error* error)
	{
		if (error == nullptr || error->message == nullptr || error->length <= 0)
			return "no message";
		return std::string(error->message, size_t(error->length));
	}
}

// Pin bgfx struct sizes to what MGS4 uses, our bgfx headers have most
// of the games customizations added, if those change these should error.
static_assert(sizeof(bgfx::Context) == 79768768);
static_assert(offsetof(bgfx::Context, m_render) == 77623936);
static_assert(offsetof(bgfx::Context, m_viewRemap) == 79664520);
static_assert(offsetof(bgfx::Context, m_view) == 79667648);
static_assert(offsetof(bgfx::Context, m_init) == 79766216);

static_assert(sizeof(bgfx::View) == 192);
static_assert(offsetof(bgfx::View, m_rect) == 16);
static_assert(offsetof(bgfx::View, m_fbh) == 160);

// Widened by extra member in MGS4, which also moves everything after
// m_bind in EncoderImpl.
static_assert(sizeof(bgfx::Binding) == 24);
static_assert(sizeof(bgfx::RenderBind) == 384);
static_assert(sizeof(bgfx::EncoderImpl) == 768);
static_assert(offsetof(bgfx::EncoderImpl, m_bind) == 256);
static_assert(offsetof(bgfx::EncoderImpl, m_numVertices) == 656);

// Unchanged in MGS4.
static_assert(sizeof(bgfx::RenderDraw) == 128);
static_assert(sizeof(bgfx::RenderItem) == 128);
static_assert(offsetof(bgfx::RenderDraw, m_instanceDataOffset) == 76);

// Narrowed by MGS4, which drops m_numVertices from it.
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
