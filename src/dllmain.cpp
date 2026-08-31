#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>

#include "hook_mgr.hpp"
#include "resource.h"
#include "plugin.hpp"

namespace Module
{
	constexpr std::string_view TargetFilename = "mgs4.exe";

	constexpr std::string_view IniFileName = "MGS4Placebo.ini";
	constexpr std::string_view LogFileName = "MGS4Placebo.log";

	void init()
	{
		if (!DllHandle)
			return;

		ExeHandle = GetModuleHandle(nullptr);

		// Fetch paths of the DLL & EXE
		DllPath = Util::GetModuleFilePath(DllHandle);
		ExePath = Util::GetModuleFilePath(ExeHandle);

		// Setup Log & INI paths, always located next to the DLL instead of the EXE
		auto dllParent = DllPath.parent_path();
		LogPath = dllParent / LogFileName;
		IniPath = dllParent / IniFileName;
	}

	void to_log()
	{
		// Print some info about the users setup to log, can come in useful for debugging issues
		spdlog::info("EXE module (base address: {:p}):", fmt::ptr(ExeHandle));
		spdlog::info("  File path: {}", ExePath.string());
		spdlog::info("  Header timestamp: {}", Util::GetModuleTimestamp(ExeHandle));
		spdlog::info("DLL module (base address: {:p}):", fmt::ptr(DllHandle));
		spdlog::info("  File path: {}", DllPath.string());
	}
};

void Plugin_Init()
{
	// setup our log & INI paths
	Module::init();

	// spdlog setup
	{
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>(true)); // Print logs to debug output
		try
		{
			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(Module::LogPath.string(), true));
		}
		catch (const std::exception&)
		{
			// spdlog failed to open log file for writing (happens in some WinStore apps)
			// let's just try to continue instead of crashing
		}

		auto combined_logger = std::make_shared<spdlog::logger>("", begin(sinks), end(sinks));
#ifdef _DEBUG
		combined_logger->set_level(spdlog::level::debug);
#else
		combined_logger->set_level(spdlog::level::debug);
#endif
		spdlog::set_default_logger(combined_logger);
		spdlog::flush_on(spdlog::level::debug);

	}

	spdlog::info("MGS4Placebo v" MODULE_VERSION_STR " - github.com/emoose/MGS4Placebo");
	Module::to_log();

	if (!Settings::read(Module::IniPath))
		spdlog::error("Settings::read - Launching game with default INI settings!");

	// Anything past this point counts as an override, and is what gets written
	// back out to the user INI.
	Settings::mark_base_values();

	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv)
	{
		bool changed = Settings::read_cmd_line(argc, argv);
		LocalFree(argv);
		if (changed)
		{
			// Prevent command-line overrides from being written back to ini
			spdlog::warn("Command-line overrides are in use, disabling INI writes.");
			Settings::DisableSettingsWrite = true;
		} 
	}

	Settings::to_log();

	Game::StartupTime = std::chrono::system_clock::now();

	HookManager::ApplyHooks();

	// Hooks declare which settings they read as they apply, so the snapshot and
	// the no-consumer check both have to wait until they've all run.
	Settings::mark_startup_values();
}

extern "C"
{
	void __declspec(dllexport) InitializeASI()
	{
		static std::once_flag flag;
		std::call_once(flag, Plugin_Init);
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, int ul_reason_for_call, LPVOID lpReserved)
{
	DisableThreadLibraryCalls(hModule);

	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		Module::DllHandle = hModule;
	}

	return TRUE;
}
