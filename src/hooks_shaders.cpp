#include "hook_mgr.hpp"
#include "plugin.hpp"

#include <cmath>
#include <fstream>
#include <string>

namespace Settings
{
	Setting<bool> ShaderPerSampleCutouts("Rendering", "MSAACutouts", true,
		"Smooths the edges of cutouts, the alpha tested foliage, fences and "
		"railings that multisampling alone does nothing for.");

	Setting<bool> ShaderDump("Shaders", "Dump", false,
		"Writes every shader the engine creates into a shader_dump folder next "
		"to this DLL. Catches any bgfx shader packs passed to the renderer.");

	Setting<bool> ShaderReplace("Shaders", "Replace", false,
		"Loads shaders from a shader_replace folder next to this DLL, by the "
		"same filename the dump uses.");
};

// Every shader in the process, the game's own and the renderer's built in ones
// alike, is created through one function. The wrapper the game calls sits above
// it and only carries what came out of the shader containers, so the renderer's
// own shaders never pass through there and cannot be seen or replaced from it.
//
// The blob is bgfx's own shader binary:
//
//   char magic[3]     'V', 'S', 'H' for a vertex shader, 'F' or 'C' otherwise
//   uint8 version
//   uint32 hashIn     names the shader, and is what a replacement is keyed on
//   uint32 hashOut    only present from version 6
//   ...uniforms, then the compiled code
namespace
{
	// The renderer describes a block of memory as a pointer and a length, and
	// takes ownership of the description when it is handed a shader.
	struct BgfxMemory
	{
		uint8_t* data;
		uint32_t size;
	};

	// Wraps a caller's buffer in one of those descriptions without copying it,
	// which is how a replacement is handed over in place of the original.
	using MakeRef_fn = const BgfxMemory*(__fastcall*)(const void* data, uint32_t size,
		void* releaseFn, void* userData);

	struct ShaderBlobInfo
	{
		char kind;
		uint8_t version;
		uint32_t linkIn;
		uint32_t linkOut;
		uint32_t id;
	};

	// The two hashes in the header link a vertex shader to the fragment shaders
	// it can feed, so they are shared rather than unique: every vertex shader
	// here carries the same zero. A hash of the whole blob names a shader on its
	// own, stays the same between runs, and is what a replacement is keyed on.
	uint32_t HashBlob(const uint8_t* data, size_t size)
	{
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < size; i++)
		{
			hash ^= data[i];
			hash *= 16777619u;
		}
		return hash;
	}

	bool ReadBlobInfo(const uint8_t* data, size_t size, ShaderBlobInfo& info)
	{
		if (data == nullptr || size < 12)
			return false;

		if (data[1] != 'S' || data[2] != 'H')
			return false;

		if (data[0] != 'V' && data[0] != 'F' && data[0] != 'C')
			return false;

		info.kind = char(data[0]);
		info.version = data[3];
		std::memcpy(&info.linkIn, data + 4, sizeof(info.linkIn));
		// Only one hash is stored before version 6, and it serves as both.
		std::memcpy(&info.linkOut, data + (info.version >= 6 ? 8 : 4), sizeof(info.linkOut));
		info.id = HashBlob(data, size);
		return true;
	}

	// Same name the dump writes and a replacement is looked up by.
	std::string BlobFileName(const ShaderBlobInfo& info)
	{
		char name[64];
		std::snprintf(name, sizeof(name), "%cSH_%08x.bin", info.kind, info.id);
		return name;
	}

	// Foliage, fences and railings are solid triangles whose shader throws away
	// the pixels where the texture is transparent. Multisampling does nothing
	// for those edges: it decides coverage at the edges of triangles, and this
	// edge is in the middle of one. The shader runs once per pixel, so every
	// sample in that pixel gets the same keep or discard.
	//
	// Direct3D runs a pixel shader once per sample instead as soon as the shader
	// asks for an input to be interpolated at the sample rather than at the
	// centre of the pixel. Each sample then gets its own texture coordinate, its
	// own alpha and its own decision, and the cutout edge comes out as smooth as
	// any triangle edge. That request is a field inside the declaration of an
	// input, so turning it on rewrites a value in place and leaves the shader
	// exactly as long as it was.
	namespace Dxbc
	{
		// The alpha the cutout shaders compare against, 127/255, which the
		// compiler left in them as a literal and which nothing else uses.
		constexpr uint32_t kAlphaTestLiteral = 0x3EFEFEFF;

		constexpr uint32_t kDeclareInput = 98;

		// Where the interpolation sits in that declaration, and what to raise it
		// to so the input is interpolated at each sample. Perspective correct
		// and not are kept apart, being a property of the input rather than of
		// the rate.
		constexpr uint32_t kInterpShift = 11;
		constexpr uint32_t kInterpMask = 0xF;

		uint32_t PerSample(uint32_t mode)
		{
			switch (mode)
			{
			case 2: case 3: return 6; // linear, and linear at the centroid
			case 4: case 5: return 7; // the same two, without perspective correction
			default: return mode;
			}
		}

		uint32_t Read(const uint8_t* at) { uint32_t v; std::memcpy(&v, at, sizeof(v)); return v; }
		void Write(uint8_t* at, uint32_t v) { std::memcpy(at, &v, sizeof(v)); }

		// Finds the instruction tokens: SHEX on newer shaders, SHDR on older.
		bool FindCode(const uint8_t* dxbc, uint32_t size, uint32_t& body, uint32_t& dwords)
		{
			if (size < 32 || std::memcmp(dxbc, "DXBC", 4) != 0)
				return false;

			const uint32_t count = Read(dxbc + 28);
			for (uint32_t i = 0; i < count; i++)
			{
				if (32 + 4 * i + 4 > size)
					return false;

				const uint32_t at = Read(dxbc + 32 + 4 * i);
				if (at + 8 > size)
					return false;

				if (std::memcmp(dxbc + at, "SHEX", 4) == 0 || std::memcmp(dxbc + at, "SHDR", 4) == 0)
				{
					body = at + 8;
					dwords = Read(dxbc + body + 4);
					return body + dwords * 4 <= size;
				}
			}
			return false;
		}

		bool TestsAlpha(const uint8_t* dxbc, uint32_t body, uint32_t dwords)
		{
			for (uint32_t i = 0; i < dwords; i++)
			{
				if (Read(dxbc + body + i * 4) == kAlphaTestLiteral)
					return true;
			}
			return false;
		}

		// Raises every interpolated input to per sample, returning how many
		// changed. An instruction's length is packed into its opcode token; a
		// zero there means the length is a dword of its own.
		uint32_t MakePerSample(uint8_t* dxbc, uint32_t body, uint32_t dwords)
		{
			uint32_t changed = 0;
			uint32_t pos = 2;

			while (pos < dwords)
			{
				uint8_t* at = dxbc + body + pos * 4;
				const uint32_t token = Read(at);

				uint32_t length = (token >> 24) & 0x7F;
				if (length == 0)
					length = (pos + 1 < dwords) ? Read(at + 4) : 0;
				if (length == 0)
					break;

				if ((token & 0x7FF) == kDeclareInput)
				{
					const uint32_t mode = (token >> kInterpShift) & kInterpMask;
					const uint32_t raised = PerSample(mode);
					if (raised != mode)
					{
						Write(at, (token & ~(kInterpMask << kInterpShift)) | (raised << kInterpShift));
						changed++;
					}
				}

				pos += length;
			}

			return changed;
		}

		// The container carries a digest of everything past it. Nothing in the
		// renderer checks it, but tools name a shader by it, so it is brought
		// back into agreement to keep an edited shader distinguishable. It is a
		// variation on MD5: the bit count goes in at the front of the final
		// block rather than the end, and a value derived from it closes it.
		void Reseal(uint8_t* dxbc, uint32_t size)
		{
			static uint32_t sines[64];
			static bool ready = false;
			if (!ready)
			{
				for (int i = 0; i < 64; i++)
					sines[i] = uint32_t(std::fabs(std::sin(i + 1.0)) * 4294967296.0);
				ready = true;
			}

			static const uint32_t shifts[64] = {
				7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
				5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
				4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
				6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
			};

			uint32_t state[4] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };

			const auto block = [&](const uint8_t* data)
			{
				uint32_t m[16];
				for (int i = 0; i < 16; i++)
					m[i] = Read(data + i * 4);

				uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
				for (int i = 0; i < 64; i++)
				{
					uint32_t f, g;
					if (i < 16)      { f = (b & c) | (~b & d);  g = i; }
					else if (i < 32) { f = (d & b) | (~d & c);  g = (5 * i + 1) % 16; }
					else if (i < 48) { f = b ^ c ^ d;           g = (3 * i + 5) % 16; }
					else             { f = c ^ (b | ~d);        g = (7 * i) % 16; }

					f += a + sines[i] + m[g];
					a = d; d = c; c = b;
					b += (f << shifts[i]) | (f >> (32 - shifts[i]));
				}
				state[0] += a; state[1] += b; state[2] += c; state[3] += d;
			};

			const uint8_t* data = dxbc + 20;
			const uint32_t length = size - 20;
			const uint32_t bits = length * 8;
			const uint32_t bitsTail = (bits >> 2) | 1;

			const uint32_t whole = length - (length % 64);
			for (uint32_t at = 0; at < whole; at += 64)
				block(data + at);

			const uint32_t rest = length - whole;
			uint8_t tail[64] = {};

			if (rest >= 56)
			{
				std::memcpy(tail, data + whole, rest);
				tail[rest] = 0x80;
				block(tail);

				std::memset(tail, 0, sizeof(tail));
				Write(tail, bits);
				Write(tail + 60, bitsTail);
				block(tail);
			}
			else
			{
				Write(tail, bits);
				std::memcpy(tail + 4, data + whole, rest);
				tail[4 + rest] = 0x80;
				Write(tail + 60, bitsTail);
				block(tail);
			}

			for (int i = 0; i < 4; i++)
				Write(dxbc + 4 + i * 4, state[i]);
		}
	}

	// Returns a copy of a cutout shader converted to run per sample, or nullptr
	// where the shader does not test alpha and needs no change. The copy is
	// never freed: the renderer only takes a reference to what it is handed and
	// reads it later, on its own thread.
	const uint8_t* MakeCutoutPerSample(const uint8_t* data, uint32_t size, uint32_t& outSize)
	{
		uint32_t at = 0;
		for (; at + 4 <= size; at++)
		{
			if (std::memcmp(data + at, "DXBC", 4) == 0)
				break;
		}
		if (at + 32 > size)
			return nullptr;

		const uint32_t containerSize = Dxbc::Read(data + at + 24);
		if (containerSize < 32 || at + containerSize > size)
			return nullptr;

		uint32_t body = 0, dwords = 0;
		if (!Dxbc::FindCode(data + at, containerSize, body, dwords))
			return nullptr;

		if (!Dxbc::TestsAlpha(data + at, body, dwords))
			return nullptr;

		uint8_t* copy = new uint8_t[size];
		std::memcpy(copy, data, size);

		if (Dxbc::MakePerSample(copy + at, body, dwords) == 0)
		{
			delete[] copy;
			return nullptr;
		}

		Dxbc::Reseal(copy + at, containerSize);

		outSize = size;
		return copy;
	}

	std::filesystem::path DumpDir() { return Module::DllPath.parent_path() / "shader_dump"; }
	std::filesystem::path ReplaceDir() { return Module::DllPath.parent_path() / "shader_replace"; }
}

class ShaderDumpHook : public Hook
{
	inline static SafetyHookInline CreateShader_hook = {};

	static void WriteDump(const ShaderBlobInfo& info, const uint8_t* data, uint64_t size)
	{
		const std::filesystem::path path = DumpDir() / BlobFileName(info);

		std::error_code ec;
		if (std::filesystem::exists(path, ec))
			return;

		std::ofstream file(path, std::ios::binary);
		if (!file)
		{
			spdlog::warn("ShaderDumpHook: could not write {}", path.string());
			return;
		}

		file.write(reinterpret_cast<const char*>(data), std::streamsize(size));
		spdlog::debug("ShaderDumpHook: dumped {} ({} bytes, link {:08x}/{:08x})",
			BlobFileName(info), size, info.linkIn, info.linkOut);
	}

	// Returns the replacement blob, or nullptr to keep the game's own. bgfx
	// only takes a reference to what it is handed and reads it on the render
	// thread, so a replacement has to outlive this call and is never freed.
	static const uint8_t* LoadReplacement(const ShaderBlobInfo& info, uint64_t& size)
	{
		const std::filesystem::path path = ReplaceDir() / BlobFileName(info);

		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return nullptr;

		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			spdlog::warn("ShaderDumpHook: could not read {}", path.string());
			return nullptr;
		}

		const std::streamsize length = file.tellg();
		file.seekg(0);

		uint8_t* buffer = new uint8_t[size_t(length)];
		if (!file.read(reinterpret_cast<char*>(buffer), length))
		{
			delete[] buffer;
			spdlog::warn("ShaderDumpHook: short read of {}", path.string());
			return nullptr;
		}

		ShaderBlobInfo check{};
		if (!ReadBlobInfo(buffer, size_t(length), check))
		{
			delete[] buffer;
			spdlog::error("ShaderDumpHook: {} is not a bgfx shader, ignored", path.string());
			return nullptr;
		}

		size = uint64_t(length);
		spdlog::info("ShaderDumpHook: replaced {} with {} bytes", BlobFileName(info), size);
		return buffer;
	}

	// The handle is returned through a pointer the caller supplies rather than
	// in a register, so the result is that same pointer.
	static uint16_t* CreateShader_dest(void* context, uint16_t* result, const BgfxMemory* memory)
	{
		if (memory == nullptr || memory->data == nullptr)
			return CreateShader_hook.call<uint16_t*>(context, result, memory);

		ShaderBlobInfo info{};
		if (!ReadBlobInfo(memory->data, memory->size, info))
		{
			spdlog::warn("ShaderDumpHook: {} byte blob is not a bgfx shader", memory->size);
			return CreateShader_hook.call<uint16_t*>(context, result, memory);
		}

		if (Settings::ShaderDump)
			WriteDump(info, memory->data, memory->size);

		// Whatever the shader ends up being, starting from the one on disk when
		// there is one so that a replacement can be converted too.
		const uint8_t* data = memory->data;
		uint32_t size = memory->size;

		if (Settings::ShaderReplace)
		{
			uint64_t replacementSize = 0;
			if (const uint8_t* replacement = LoadReplacement(info, replacementSize))
			{
				data = replacement;
				size = uint32_t(replacementSize);
			}
		}

		if (Settings::ShaderPerSampleCutouts && info.kind == 'F')
		{
			uint32_t convertedSize = 0;
			if (const uint8_t* converted = MakeCutoutPerSample(data, size, convertedSize))
			{
				spdlog::debug("ShaderDumpHook: {} converted to run per sample", BlobFileName(info));
				data = converted;
				size = convertedSize;
			}
		}

		if (data == memory->data)
			return CreateShader_hook.call<uint16_t*>(context, result, memory);

		auto makeRef = Module::fn_ptr<MakeRef_fn>(0x765050);

		// The original description is left behind rather than handed over, so
		// the renderer never releases it. That costs one small allocation per
		// shader changed, once, and is the price of not having to know how the
		// original was allocated.
		if (const BgfxMemory* swapped = makeRef(data, size, nullptr, nullptr))
			return CreateShader_hook.call<uint16_t*>(context, result, swapped);

		return CreateShader_hook.call<uint16_t*>(context, result, memory);
	}

public:
	std::string_view description() override
	{
		return "ShaderDumpHook";
	}

	bool validate() override
	{
		return Settings::ShaderDump || Settings::ShaderReplace || Settings::ShaderPerSampleCutouts;
	}

	void declare_settings() override
	{
		// Shaders are created once as the engine loads them.
		Settings::ShaderDump.needs_restart();
		Settings::ShaderReplace.needs_restart();
		Settings::ShaderPerSampleCutouts.needs_restart();
	}

	bool apply() override
	{
		//   mov [rsp+18h], r8 / push rbp / push rsi / push rdi
		if (!Module::code_matches(0x75BA10, { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57 }))
			return false;

		if (Settings::ShaderDump)
		{
			std::error_code ec;
			std::filesystem::create_directories(DumpDir(), ec);
			if (ec)
			{
				spdlog::error("ShaderDumpHook: could not create {}: {}", DumpDir().string(), ec.message());
				return false;
			}
			spdlog::info("ShaderDumpHook: dumping shaders to {}", DumpDir().string());
		}

		if (Settings::ShaderReplace)
			spdlog::info("ShaderDumpHook: reading replacements from {}", ReplaceDir().string());

		CreateShader_hook = safetyhook::create_inline(
			Module::exe_ptr(0x75BA10), CreateShader_dest);

		return bool(CreateShader_hook);
	}

	static ShaderDumpHook instance;
};
ShaderDumpHook ShaderDumpHook::instance;
