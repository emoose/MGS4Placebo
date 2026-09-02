# bgfx headers

The game renders through bgfx, forked from commit
`dcf926f624b9642769f8486e789ec26c74e34f4b` (2023-02-02) with Konami's own
changes on top.
The headers in this folder try to match the changes in MGS4.

| File | Origin |
|---|---|
| `bgfx.h` | upstream `include/bgfx/bgfx.h`, modified |
| `defines.h` | upstream `include/bgfx/defines.h`, unmodified |
| `tinystl/` | upstream, vendored from bx, unmodified |
| `config.h` | ours: the layout-relevant part of upstream `src/config.h`, with this build's values |
| `internal.hpp` | ours: mirrors of the types in upstream `src/bgfx_p.h`, plus the bx containers they are built from |

Upstream's public headers need only `<stdarg.h>`, `<stdint.h>` and `<stdlib.h>`,
so they are vendored as-is. `bgfx_p.h` cannot be, since it pulls in all of bx
and bimg for behaviour we don't need, so `internal.hpp` mirrors the
types it needs.

`Frame` and `EncoderImpl` are fully typed. `Context` still pads bx's threading
primitives, four bgfx helpers nothing here reaches into (`NonLocalAllocator`,
`ClearQuad`, `TextVideoMemBlitter`, `UpdateBatchT`), and the two `m_pad*` runs
below.

Addresses of these structures inside the game live in `../game_bgfx.hpp`, which
also asserts every size and offset here against what the game was measured to
use.

## Changes from upstream

**`RendererType::Enum` gains `Gnmp`** after `WebGPU`, so `Count` is 13, not 12.

**`Binding` is 24 bytes, not 12.** The fork adds an 8-byte member at +8, filled
from a fifth argument to `EncoderImpl::setTexture` that upstream does not have.
This widens `RenderBind` to 384, takes `EncoderImpl` from 576 to 768, and grows
`Frame` by 12 MB.

**`DynamicVertexBuffer` is 24 bytes, not 28.** The fork drops `m_numVertices`,
deriving the count from `m_size / m_stride` in `EncoderImpl::setVertexBuffer`,
and stores a byte offset in `m_startVertex` where upstream stores a vertex
index.

**`Caps` is 336 bytes, not 328**, carried as `_padMgs4[8]`. Every field is at
its upstream offset and nothing in the game touches the last 8 bytes, so they
are most likely trailing alignment.

## Config

`config.h` sizes the arrays in `internal.hpp`, so layouts follow from it rather
than from numbers written at each declaration.

| Define | Upstream | This build |
|---|---|---|
| `BGFX_CONFIG_MAX_VIEWS` | 256 | 512 |
| `BGFX_CONFIG_MAX_VERTEX_LAYOUTS` | 64 | 256 |
| `BGFX_CONFIG_MAX_SHADERS` | 512 | 4096 |
| `BGFX_CONFIG_SORT_KEY_NUM_BITS_PROGRAM` | 9 | 10 (so `MAX_PROGRAMS` 512 -> 1024) |
| `BGFX_CONFIG_MAX_FRAME_BUFFERS` | 128 | 256 |
| `BGFX_CONFIG_MAX_OCCLUSION_QUERIES` | 256 | 2048 |

Everything else in `config.h` matches upstream.

## Unidentified additions

`Context` has some added fields that aren't in upstream bgfx, and haven't been
fully figured out yet:

- **`m_pad0`, 135,184 bytes** after `m_shaderRef`: 4096 x 32 bytes, one entry
  per shader, plus a `{u64 capacity, u64 count}` container holding 4096 bytes.
- **`m_pad1`, 89,128 bytes** after `m_programRef`: 1024 x 6 bytes, one entry per
  program, a `{u64 1024, u64 0}` container with 1024 bytes, an 81,920 byte array
  (5120 x 16, initialised to `0x000CFFFF`), and a 16 byte trailer.

Every count is a multiple of `MAX_SHADERS` or `MAX_PROGRAMS`, so this reads as
one added shader/program tracking subsystem rather than unrelated fields.
