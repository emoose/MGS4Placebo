#pragma once

#include <cstring>
#include <filesystem>
#include <initializer_list>

#include <spdlog/spdlog.h>

#include "settings.hpp"

extern void DInput_RegisterNewDevices(); // hooks_input.cpp
extern void SetVibration(int userId, float leftMotor, float rightMotor); // hooks_forcefeedback.cpp
extern void AudioHooks_Update(int numUpdates); // hooks_audio.cpp
extern void CDSwitcher_ReadIni(const std::filesystem::path& iniPath);

namespace Module
{
	// Info about our module
	inline HMODULE DllHandle{ 0 };
	inline std::filesystem::path DllPath{};

	// Info about the module we've been loaded into
	inline HMODULE ExeHandle{ 0 };
	inline std::filesystem::path ExePath{};

	inline std::filesystem::path LogPath{};
	inline std::filesystem::path IniPath{};

	template <typename T>
	inline T* exe_ptr(uintptr_t offset) { if (ExeHandle) return (T*)(((uintptr_t)ExeHandle) + offset); else return nullptr; }
	inline uint8_t* exe_ptr(uintptr_t offset) { if (ExeHandle) return (uint8_t*)(((uintptr_t)ExeHandle) + offset); else return nullptr; }

	template <typename T>
	inline T fn_ptr(uintptr_t offset) { if (ExeHandle) return (T)(((uintptr_t)ExeHandle) + offset); else return nullptr; }

	// Deduce the type by providing it as an argument, no need for ugly decltype stuff
	template <typename T>
	inline T fn_ptr(uintptr_t offset, T& var)
	{
		if (ExeHandle)
			return reinterpret_cast<T>(((uintptr_t)ExeHandle) + offset);
		else
			return nullptr;
	}

	// Checks that an offset a hook is about to write to still holds the
	// instructions it was found at. Every hook offset is hardcoded for one build
	// of the game, so a build that doesn't match has to leave the hook inactive
	// rather than write over whatever now sits there.
	inline bool code_matches(uintptr_t offset, std::initializer_list<uint8_t> expected)
	{
		const uint8_t* code = exe_ptr(offset);
		if (code && std::memcmp(code, expected.begin(), expected.size()) == 0)
			return true;

		spdlog::error("Unexpected code at exe+{:X}, hook skipped", offset);
		return false;
	}

	void init();
}

namespace Game
{
	enum class GamepadType
	{
		None,
		PC,
		Xbox,
		PS,
		Switch
	};

	inline static const char* PadTypes[] =
	{
		"None",
		"PC",
		"Xbox",
		"PlayStation",
		"Switch"
	};

	inline std::chrono::system_clock::time_point StartupTime;
	inline float DeltaTime = (1.f / 60.f);

	inline GamepadType CurrentPadType = GamepadType::PC;
	inline GamepadType ForcedPadType = GamepadType::None;
};

namespace Util
{
	inline uint32_t GetModuleTimestamp(HMODULE moduleHandle)
	{
		if (!moduleHandle)
			return 0;

		uint8_t* moduleData = (uint8_t*)moduleHandle;
		const IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)moduleData;
		const IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(moduleData + dosHeader->e_lfanew);
		return ntHeaders->FileHeader.TimeDateStamp;
	}

	// Fetches path of module as std::filesystem::path, resizing buffer automatically if path length above MAX_PATH
	inline std::filesystem::path GetModuleFilePath(HMODULE moduleHandle)
	{
		std::vector<wchar_t> buffer(MAX_PATH, L'\0');

		DWORD result = GetModuleFileNameW(moduleHandle, buffer.data(), buffer.size());
		while (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			// Buffer was too small, resize and try again
			buffer.resize(buffer.size() * 2, L'\0');
			result = GetModuleFileNameW(moduleHandle, buffer.data(), buffer.size());
		}

		return std::wstring(buffer.data(), result);
	}

	inline uint32_t BitCount(uint32_t n)
	{
		n = n - ((n >> 1) & 0x55555555);          // put count of each 2 bits into those 2 bits
		n = (n & 0x33333333) + ((n >> 2) & 0x33333333); // put count of each 4 bits into those 4 bits
		n = (n + (n >> 4)) & 0x0F0F0F0F;          // put count of each 8 bits into those 8 bits
		n = n + (n >> 8);                         // put count of each 16 bits into their lowest 8 bits
		n = n + (n >> 16);                        // put count of each 32 bits into their lowest 8 bits
		return n & 0x0000003F;                    // return the count
	}

	// Function to trim spaces from the start of a string
	inline std::string ltrim(const std::string& s)
	{
		auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char ch)
		{
			return std::isspace(ch);
		});
		return std::string(start, s.end());
	}

	// Function to trim spaces from the end of a string
	inline std::string rtrim(const std::string& s)
	{
		auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch)
		{
			return std::isspace(ch);
		});
		return std::string(s.begin(), end.base());
	}

	// Function to trim spaces from both ends of a string
	inline std::string trim(const std::string& s)
	{
		return ltrim(rtrim(s));
	}
}

inline void WaitForDebugger()
{
#ifdef _DEBUG
	while (!IsDebuggerPresent())
	{
	}
#endif
}
