#pragma once

// bgfx's internal types, as MGS4 lays them out.
//
// These live in bgfx's src/bgfx_p.h upstream, which cannot be vendored by itself
// as it pulls in a lot of bx/bimg stuff.
// Only struct layouts matter for us, so types are mirrored here instead.
//
// Sizes and offsets were read out of a bgfx build made to match the game, and
// are asserted in game_bgfx.hpp against what the game itself was measured to use.
// Where the fork differs from upstream it is called out on the field.

#include <cstddef>
#include <cstdint>

#include "bgfx.h"
#include "config.h"

namespace bgfx
{
	struct TinyStlAllocator
	{
		static void* static_allocate(size_t _bytes);
		static void static_deallocate(void* _ptr, size_t _bytes);
	};
}

#define TINYSTL_ALLOCATOR bgfx::TinyStlAllocator
#include "tinystl/unordered_set.h"

// bx types, mirrored for layout only. bx is 99+ headers and nothing here needs
// its behaviour, just structs used in bgfx.
namespace bx
{
	struct Error
	{
		const char* message;
		int32_t length;
		bool terminated;
		uint32_t code;
	};

	struct StringView
	{
		const char* m_ptr;
		int32_t m_len;
		bool m_0terminated;
		uint8_t _pad0D[3];
	};

	struct StringT
	{
		const char* m_ptr;
		int32_t m_len;
		bool m_0terminated;
		uint8_t _pad0D[3];
		int32_t m_capacity;
		uint8_t _pad14[4];
	};

	struct HandleAlloc
	{
		uint16_t m_numHandles;
		uint16_t m_maxHandles;
	};

	template <uint16_t MaxHandlesT>
	struct HandleAllocT
	{
		uint16_t m_numHandles;
		uint16_t m_maxHandles;
		uint16_t m_dense[MaxHandlesT];
		uint16_t m_sparse[MaxHandlesT];
	};

	template <uint32_t MaxCapacityT, typename KeyT = uint32_t>
	struct HandleHashMapT
	{
		uint32_t m_maxCapacity;
		uint32_t m_numElements;
		KeyT m_key[MaxCapacityT];
		uint16_t m_handle[MaxCapacityT];
	};
}

namespace bgfx
{
	struct Rect
	{
		uint16_t m_x;
		uint16_t m_y;
		uint16_t m_width;
		uint16_t m_height;
	};

	struct Clear
	{
		uint8_t m_index[8];
		float m_depth;
		uint8_t m_stencil;
		uint8_t _pad13;
		uint16_t m_flags;
	};

	struct alignas(16) Matrix4
	{
		float un[16];
	};

	struct alignas(32) View
	{
		Clear m_clear;
		Rect m_rect;
		Rect m_scissor;
		Matrix4 m_view;
		Matrix4 m_proj;
		FrameBufferHandle m_fbh;
		uint8_t m_mode;
	};

	struct Binding
	{
		uint32_t m_samplerFlags;
		uint32_t _pad04;
		uint64_t _unkMgs4_08; // MGS4: Unknown field added here?
		uint16_t m_idx;
		uint8_t m_type;
		uint8_t m_format;
		uint8_t m_access;
		uint8_t m_mip;
	};

	struct RenderBind
	{
		Binding m_bind[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS];
	};

	struct Stream
	{
		uint32_t m_startVertex;
		VertexBufferHandle m_handle;
		VertexLayoutHandle m_layoutHandle;
	};

	struct alignas(16) RenderDraw
	{
		Stream m_stream[BGFX_CONFIG_MAX_VERTEX_STREAMS];
		uint64_t m_stateFlags;
		uint64_t m_stencil;
		uint32_t m_rgba;
		uint32_t m_uniformBegin;
		uint32_t m_uniformEnd;
		uint32_t m_startMatrix;
		uint32_t m_startIndex;
		uint32_t m_numIndices;
		uint32_t m_numVertices;
		uint32_t m_instanceDataOffset;
		uint32_t m_numInstances;
		uint16_t m_instanceDataStride;
		uint16_t m_startIndirect;
		uint16_t m_numIndirect;
		uint16_t _pad90;
		uint32_t m_numIndirectIndex;
		uint16_t m_numMatrices;
		uint16_t m_scissor;
		uint8_t m_submitFlags;
		uint8_t m_streamMask;
		uint8_t m_uniformIdx;
		uint8_t _pad67;
		IndexBufferHandle m_indexBuffer;
		VertexBufferHandle m_instanceDataBuffer;
		IndirectBufferHandle m_indirectBuffer;
		IndexBufferHandle m_numIndirectBuffer;
		OcclusionQueryHandle m_occlusionQuery;
		uint8_t _pad72[14];
	};

	struct alignas(32) RenderCompute
	{
		uint32_t m_uniformBegin;
		uint32_t m_uniformEnd;
		uint32_t m_startMatrix;
		IndirectBufferHandle m_indirectBuffer;
		uint16_t _pad0E;
		uint32_t m_numX;
		uint32_t m_numY;
		uint32_t m_numZ;
		uint16_t m_startIndirect;
		uint16_t m_numIndirect;
		uint16_t m_numMatrices;
		uint8_t m_submitFlags;
		uint8_t m_uniformIdx;
		uint8_t _pad24[28];
	};

	union alignas(32) RenderItem
	{
		RenderDraw draw;
		RenderCompute compute;
	};

	struct SortKey
	{
		uint32_t m_depth;
		uint32_t m_seq;
		ProgramHandle m_program;
		uint16_t m_view;
		uint8_t m_blend;
		uint8_t _pad0D[3];
	};

	struct alignas(64) EncoderImpl
	{
		void* m_frame;
		SortKey m_key;
		uint8_t _pad18[40];
		RenderDraw m_draw;
		RenderCompute m_compute;
		RenderBind m_bind;
		uint32_t m_numSubmitted;
		uint32_t m_numDropped;
		uint32_t m_uniformBegin;
		uint32_t m_uniformEnd;
		uint32_t m_numVertices[BGFX_CONFIG_MAX_VERTEX_STREAMS];
		uint8_t m_uniformIdx;
		bool m_discard;
		uint8_t _pad2A2[6];
		tinystl::unordered_set<uint16_t> m_uniformSet;
		tinystl::unordered_set<uint16_t> m_occlusionQuerySet;
		int64_t m_cpuTimeBegin;
		int64_t m_cpuTimeEnd;
	};

	struct Handle
	{
		uint16_t type;
		uint16_t idx;
	};

	// A draw points into these with RenderDraw::m_startMatrix and m_scissor.
	struct MatrixCache
	{
		Matrix4 m_cache[BGFX_CONFIG_MAX_MATRIX_CACHE];
		uint32_t m_num;
	};

	struct RectCache
	{
		Rect m_cache[BGFX_CONFIG_MAX_RECT_CACHE];
		uint32_t m_num;
	};

	struct FrameCache
	{
		MatrixCache m_matrixCache;
		RectCache m_rectCache;
	};

	struct alignas(64) BlitItem
	{
		uint16_t m_srcX;
		uint16_t m_srcY;
		uint16_t m_srcZ;
		uint16_t m_dstX;
		uint16_t m_dstY;
		uint16_t m_dstZ;
		uint16_t m_width;
		uint16_t m_height;
		uint16_t m_depth;
		uint8_t m_srcMip;
		uint8_t m_dstMip;
		Handle m_src;
		Handle m_dst;
	};

	struct CommandBuffer
	{
		uint8_t* m_buffer;
		uint32_t m_pos;
		uint32_t m_size;
		uint32_t m_capacity;
		uint32_t m_minCapacity;
	};

	struct ScreenShot
	{
		uint8_t filePath[1024]; // bx::FilePath
		FrameBufferHandle handle;
	};

	struct IndexBuffer
	{
		bx::StringT m_name;
		uint32_t m_size;
		uint16_t m_flags;
		uint8_t _pad1E[2];
	};

	struct VertexBuffer
	{
		bx::StringT m_name;
		uint32_t m_size;
		uint16_t m_stride;
		uint8_t _pad1E[2];
	};

	struct FrameBufferRef
	{
		bx::StringT m_name;
		uint16_t m_width;
		uint16_t m_height;
		uint8_t _pad1C[4];
		union
		{
			TextureHandle m_th[8];
			void* m_nwh;
		} un;
		bool m_window;
		uint8_t _pad31[7];
	};

	struct VertexLayoutRef
	{
		bx::HandleHashMapT<BGFX_CONFIG_MAX_VERTEX_LAYOUTS*2> m_vertexLayoutMap;
		uint16_t m_refCount[BGFX_CONFIG_MAX_VERTEX_LAYOUTS];
		VertexLayoutHandle m_vertexBufferRef[BGFX_CONFIG_MAX_VERTEX_BUFFERS];
		VertexLayoutHandle m_dynamicVertexBufferRef[BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS];
	};

	template <typename HandleT, uint16_t MaxHandlesT>
	struct FreeHandle
	{
		uint16_t m_num;
		HandleT m_queue[MaxHandlesT];
	};

	struct DynamicIndexBuffer
	{
		IndexBufferHandle m_handle;
		uint16_t _pad02;
		uint32_t m_offset;
		uint32_t m_size;
		uint32_t m_startIndex;
		uint16_t m_flags;
		uint16_t _pad12;
	};

	// MGS4: m_numVertices field removed, count is derived from
	// m_size / m_stride instead, and stores a byte offset in m_startVertex
	// rather than a vertex index. 24 bytes here, 28 in upstream bgfx.
	struct DynamicVertexBuffer
	{
		VertexBufferHandle m_handle;
		uint16_t _pad02;
		uint32_t m_offset;
		uint32_t m_size;
		uint32_t m_startVertex;
		uint16_t m_stride;
		VertexLayoutHandle m_layoutHandle;
		uint16_t m_flags;
		uint16_t _pad16;
	};

	struct ProgramRef
	{
		ShaderHandle m_vsh;
		ShaderHandle m_fsh;
		int16_t m_refCount;
	};

	struct ShaderRef
	{
		UniformHandle* m_uniforms;
		bx::StringT m_name;
		uint32_t m_hashIn;
		uint32_t m_hashOut;
		uint16_t m_num;
		int16_t m_refCount;
		uint8_t _pad2C[4];
	};

	struct UniformRef
	{
		bx::StringT m_name;
		UniformType::Enum m_type;
		uint16_t m_num;
		int16_t m_refCount;
	};

	struct TextureRef
	{
		bx::StringT m_name;
		void* m_ptr;
		uint64_t m_flags;
		uint32_t m_storageSize;
		int16_t m_refCount;
		uint8_t m_bbRatio;
		uint8_t _pad2F;
		uint16_t m_width;
		uint16_t m_height;
		uint16_t m_depth;
		uint8_t m_format;
		uint8_t m_numSamples;
		uint8_t m_numMips;
		uint8_t _pad39;
		uint16_t m_numLayers;
		bool m_owned;
		bool m_immutable;
		bool m_cubeMap;
		uint8_t _pad3F;
	};

	struct alignas(64) Frame
	{
		uint16_t m_viewRemap[BGFX_CONFIG_MAX_VIEWS];
		float m_colorPalette[BGFX_CONFIG_MAX_COLOR_PALETTE][4];
		View m_view[BGFX_CONFIG_MAX_VIEWS];
		int32_t m_occlusion[BGFX_CONFIG_MAX_OCCLUSION_QUERIES];
		uint64_t m_sortKeys[BGFX_CONFIG_MAX_DRAW_CALLS+1];
		uint16_t m_sortValues[BGFX_CONFIG_MAX_DRAW_CALLS+1];
		RenderItem m_renderItem[BGFX_CONFIG_MAX_DRAW_CALLS+1];
		RenderBind m_renderItemBind[BGFX_CONFIG_MAX_DRAW_CALLS+1];
		uint32_t m_blitKeys[BGFX_CONFIG_MAX_BLIT_ITEMS+1];
		uint8_t _pad020BB504[60];
		BlitItem m_blitItem[BGFX_CONFIG_MAX_BLIT_ITEMS+1];
		FrameCache m_frameCache;
		void** m_uniformBuffer;
		uint32_t m_numRenderItems;
		uint16_t m_numBlitItems;
		uint8_t _pad024D35AE[2];
		uint32_t m_iboffset;
		uint32_t m_vboffset;
		TransientIndexBuffer* m_transientIb;
		TransientVertexBuffer* m_transientVb;
		Resolution m_resolution;
		uint32_t m_debug;
		ScreenShot m_screenShot[4];
		uint8_t m_numScreenShots;
		uint8_t _pad024D45E9[7];
		CommandBuffer m_cmdPre;
		CommandBuffer m_cmdPost;
		FreeHandle<IndexBufferHandle, BGFX_CONFIG_MAX_INDEX_BUFFERS> m_freeIndexBuffer;
		FreeHandle<VertexLayoutHandle, BGFX_CONFIG_MAX_VERTEX_LAYOUTS> m_freeVertexLayout;
		FreeHandle<VertexBufferHandle, BGFX_CONFIG_MAX_VERTEX_BUFFERS> m_freeVertexBuffer;
		FreeHandle<ShaderHandle, BGFX_CONFIG_MAX_SHADERS> m_freeShader;
		FreeHandle<ProgramHandle, BGFX_CONFIG_MAX_PROGRAMS> m_freeProgram;
		FreeHandle<TextureHandle, BGFX_CONFIG_MAX_TEXTURES> m_freeTexture;
		FreeHandle<FrameBufferHandle, BGFX_CONFIG_MAX_FRAME_BUFFERS> m_freeFrameBuffer;
		FreeHandle<UniformHandle, BGFX_CONFIG_MAX_UNIFORMS> m_freeUniform;
		void* m_textVideoMem;
		Stats m_perfStats;
		ViewStats m_viewStats[BGFX_CONFIG_MAX_VIEWS];
		int64_t m_waitSubmit;
		int64_t m_waitRender;
		uint32_t m_frameNum;
		bool m_capture;
	};

	// MGS4: m_pad0 and m_pad1 added with unknown type. alignas(64) to make it pad to the 79768768 the game uses.
	struct alignas(64) Context
	{
		uint8_t m_renderSem[128];
		uint8_t m_apiSem[128];
		uint8_t m_encoderEndSem[128];
		uint8_t m_encoderApiLock[64];
		uint8_t m_resourceApiLock[64];
		uint8_t m_thread[464];
		void* m_encoderStats;
		void* m_encoder0;
		EncoderImpl* m_encoder;
		uint32_t m_numEncoders;
		uint8_t _pad000003EC[4];
		bx::HandleAlloc* m_encoderHandle;
		uint8_t _pad000003F8[8];
		Frame m_frame[2];
		Frame* m_render;
		Frame* m_submit;
		uint64_t m_tempKeys[BGFX_CONFIG_MAX_DRAW_CALLS];
		uint16_t m_tempValues[BGFX_CONFIG_MAX_DRAW_CALLS];
		uint8_t _pad04AA7286[2];
		IndexBuffer m_indexBuffers[BGFX_CONFIG_MAX_INDEX_BUFFERS];
		VertexBuffer m_vertexBuffers[BGFX_CONFIG_MAX_VERTEX_BUFFERS];
		DynamicIndexBuffer m_dynamicIndexBuffers[BGFX_CONFIG_MAX_DYNAMIC_INDEX_BUFFERS];
		DynamicVertexBuffer m_dynamicVertexBuffers[BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS];
		uint16_t m_numFreeDynamicIndexBufferHandles;
		uint16_t m_numFreeDynamicVertexBufferHandles;
		uint16_t m_numFreeOcclusionQueryHandles;
		DynamicIndexBufferHandle m_freeDynamicIndexBufferHandle[BGFX_CONFIG_MAX_DYNAMIC_INDEX_BUFFERS];
		DynamicVertexBufferHandle m_freeDynamicVertexBufferHandle[BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS];
		OcclusionQueryHandle m_freeOcclusionQueryHandle[BGFX_CONFIG_MAX_OCCLUSION_QUERIES];
		uint8_t _pad04B1828E[2];
		uint8_t m_dynIndexBufferAllocator[56];
		bx::HandleAllocT<BGFX_CONFIG_MAX_DYNAMIC_INDEX_BUFFERS> m_dynamicIndexBufferHandle;
		uint8_t _pad04B1C2CC[4];
		uint8_t m_dynVertexBufferAllocator[56];
		bx::HandleAllocT<BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS> m_dynamicVertexBufferHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_INDEX_BUFFERS> m_indexBufferHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_VERTEX_LAYOUTS> m_layoutHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_VERTEX_BUFFERS> m_vertexBufferHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_SHADERS> m_shaderHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_PROGRAMS> m_programHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_TEXTURES> m_textureHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_FRAME_BUFFERS> m_frameBufferHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_UNIFORMS> m_uniformHandle;
		bx::HandleAllocT<BGFX_CONFIG_MAX_OCCLUSION_QUERIES> m_occlusionQueryHandle;
		bx::HandleHashMapT<BGFX_CONFIG_MAX_UNIFORMS*2> m_uniformHashMap;
		UniformRef m_uniformRef[BGFX_CONFIG_MAX_UNIFORMS];
		bx::HandleHashMapT<BGFX_CONFIG_MAX_SHADERS*2> m_shaderHashMap;
		ShaderRef m_shaderRef[BGFX_CONFIG_MAX_SHADERS];
		
		uint8_t m_pad0[135184]; // MGS4: 4096 x 32 bytes plus a 4112 byte container.

		bx::HandleHashMapT<BGFX_CONFIG_MAX_PROGRAMS*2> m_programHashMap;
		ProgramRef m_programRef[BGFX_CONFIG_MAX_PROGRAMS];
		
		uint8_t m_pad1[89128]; // MGS4: a 1048 byte container, 5120 x 16 bytes, and a trailer.

		TextureRef m_textureRef[BGFX_CONFIG_MAX_TEXTURES];
		FrameBufferRef m_frameBufferRef[BGFX_CONFIG_MAX_FRAME_BUFFERS];
		VertexLayoutRef m_vertexLayoutRef;
		uint16_t m_viewRemap[BGFX_CONFIG_MAX_VIEWS];
		uint32_t m_seq[BGFX_CONFIG_MAX_VIEWS];
		uint8_t _pad04BFA188[56];
		View m_view[BGFX_CONFIG_MAX_VIEWS];
		float m_clearColor[BGFX_CONFIG_MAX_COLOR_PALETTE][4];
		uint8_t m_colorPaletteDirty;
		uint8_t _pad04C122C1[7];
		Init m_init;
		int64_t m_frameTimeLast;
		uint32_t m_frames;
		uint32_t m_debug;
		int64_t m_rtMemoryUsed;
		int64_t m_textureMemoryUsed;
		uint8_t m_textVideoMemBlitter[112];
		uint8_t m_clearQuad[100];
		uint8_t _pad04C12434[4];
		void* m_renderCtx;
		bool m_rendererInitialized;
		bool m_exit;
		bool m_flipAfterRender;
		bool m_singleThreaded;
		bool m_flipped;
		uint8_t _pad04C12445[59];
		uint8_t m_textureUpdateBatch[2052];
	};

}
