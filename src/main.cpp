#include <stdinclude.hpp>
#include <./dllproxy/proxy.hpp>

extern bool init_hook();
extern void uninit_hook();
extern void start_console();

bool compatible_mode = false;
bool g_dump_entries = false;
bool g_enable_logger = false;
bool g_enable_console = false;
int g_max_fps = -1;
bool g_unlock_size = false;
float g_ui_scale = 1.0f;
float g_aspect_ratio = 16.f / 9.f;
bool g_replace_font = true;
bool g_auto_fullscreen = true;
bool g_skip_single_instance_check = true;
std::string g_notifier_host = "http://127.0.0.1:4693";
std::string g_savedata_path = "";

namespace
{
	void create_debug_console()
	{
		AllocConsole();

		// open stdout stream
		auto _ = freopen("CONOUT$", "w+t", stdout);
		_ = freopen("CONOUT$", "w", stderr);
		_ = freopen("CONIN$", "r", stdin);

		SetConsoleTitle("Umamusume - Debug Console");

		// set this to avoid turn japanese texts into question mark
		SetConsoleOutputCP(65001);
		std::locale::global(std::locale(""));

		wprintf(L"\u30a6\u30de\u5a18 Localify Patch Loaded! - By GEEKiDoS, Modified By Lipi\n");
	}

	std::vector<std::string> read_config()
	{
		std::ifstream config_stream{ "config.json" };
		std::vector<std::string> dicts{};

		if (!config_stream.is_open())
			return dicts;

		rapidjson::IStreamWrapper wrapper{ config_stream };
		rapidjson::Document document;

		document.ParseStream(wrapper);

		if (!document.HasParseError() && !compatible_mode)
		{
			g_enable_console = document["enableConsole"].GetBool();
			g_enable_logger = document["enableLogger"].GetBool();
			g_dump_entries = document["dumpStaticEntries"].GetBool();
			g_max_fps = document["maxFps"].GetInt();
			g_unlock_size = document["unlockSize"].GetBool();
			g_ui_scale = document["uiScale"].GetFloat();
			g_replace_font = document["replaceFont"].GetBool();
			g_auto_fullscreen = document["autoFullscreen"].GetBool();

			// Looks like not working for now
			// g_aspect_ratio = document["customAspectRatio"].GetFloat();

			auto& dicts_arr = document["dicts"];
			auto len = dicts_arr.Size();

			for (size_t i = 0; i < len; ++i)
			{
				auto dict = dicts_arr[i].GetString();

				dicts.push_back(dict);
			}
		}
		if (document.HasMember("notifier_host"))
			g_notifier_host = document["notifier_host"].GetString();
		if (document.HasMember("savedata_path"))
			g_savedata_path = document["savedata_path"].GetString();
		if (document.HasMember("skip_single_instance_check"))
			g_skip_single_instance_check = document["skip_single_instance_check"].GetBool();

		config_stream.close();
		return dicts;
	}
}


void* CreateMutex_orig = nullptr;
void* CreateMutexDetour(
	LPSECURITY_ATTRIBUTES lpMutexAttributes,
	BOOL                  bInitialOwner,
	LPCSTR                lpName)
{
	if (lpName != NULL && strstr(lpName, "-SingleInstanceMutex-")) {
		std::ostringstream oss;
		oss << lpName << '-' << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		return reinterpret_cast<decltype(CreateMutex)*>(CreateMutex_orig)(lpMutexAttributes, bInitialOwner, oss.str().c_str());
	}

	return reinterpret_cast<decltype(CreateMutex)*>(CreateMutex_orig)(lpMutexAttributes, bInitialOwner, lpName);
}
int __stdcall DllMain(HINSTANCE hInstance, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		// the DMM Launcher set start path to system32 wtf????
		std::string module_name;
		module_name.resize(MAX_PATH);
		module_name.resize(GetModuleFileName(nullptr, module_name.data(), MAX_PATH));

		std::filesystem::path module_path(module_name);

		LPSTR proxy_filename_buffer = new CHAR[MAX_PATH];
		GetModuleFileNameA(hInstance, proxy_filename_buffer, MAX_PATH);
		std::filesystem::path proxy_filename_path(proxy_filename_buffer);
		std::string proxy_filename = proxy_filename_path.filename().string();
		std::for_each(proxy_filename.begin(), proxy_filename.end(), [](char& character) { character = ::tolower(character); });
		compatible_mode = proxy_filename != "version.dll";
		proxy::init_proxy(proxy_filename);

		// check name
		if (module_path.filename() != "umamusume.exe")
			return 1;

		std::filesystem::current_path(
			module_path.parent_path()
		);

		auto dicts = read_config();

		if (g_enable_console)
			create_debug_console();

		MH_Initialize();
		if (g_skip_single_instance_check) {
			MH_CreateHook(&CreateMutex, &CreateMutexDetour, &CreateMutex_orig);
			MH_EnableHook(&CreateMutex);
		}
		std::thread init_thread([dicts]() {
			logger::init_logger();
			local::load_textdb(&dicts);
			init_hook();

			if (g_enable_console)
				start_console();
			});
		init_thread.detach();
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		uninit_hook();
		logger::close_logger();
	}

	return 1;
}
