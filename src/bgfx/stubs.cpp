// Definitions for bgfx members the vendored headers declare but whose bodies
// live in bgfx source that is not built here.
//
// Rather than invent a body, each forwards to the game's own copy, so the
// behaviour is whatever the game's bgfx actually does.

#include "../hook_mgr.hpp"
#include "../plugin.hpp"
#include "../game_bgfx.hpp"

namespace bgfx
{
	VertexLayout::VertexLayout()
	{
		using Ctor = VertexLayout* (*)(VertexLayout*);
		const auto ctor = Module::fn_ptr<Ctor>(BgfxGame::Offsets::VertexLayoutCtor);
		if (ctor != nullptr)
			ctor(this);
	}
}
