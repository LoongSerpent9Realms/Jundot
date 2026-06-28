/**************************************************************************/
/*  crash_handler_windows_signal.cpp                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "crash_handler_windows.h"

#include "core/config/project_settings.h"
#include "core/object/script_language.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/version.h"

#ifdef CRASH_HANDLER_EXCEPTION

#include <thirdparty/libbacktrace/backtrace.h>

#include <cxxabi.h>
#include <psapi.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <iterator>
#include <vector>

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#pragma pack(push, before_imagehlp, 8)
#include <imagehlp.h>
#pragma pack(pop, before_imagehlp)

struct CrashHandlerData {
	int64_t index = 0;
	backtrace_state *state = nullptr;
	int64_t offset = 0;
	int64_t base = 0;
	uint64_t pc = 0;
	HANDLE process = nullptr;
	bool sym_ok = false;
};

struct module_data {
	std::string image_name;
	std::string module_name;
	void *base_address = nullptr;
	DWORD load_size;
};

class get_mod_info {
	HANDLE process;

public:
	get_mod_info(HANDLE h) :
			process(h) {}

	module_data operator()(HMODULE module) {
		module_data ret;
		char temp[4096];
		MODULEINFO mi;

		GetModuleInformation(process, module, &mi, sizeof(mi));
		ret.base_address = mi.lpBaseOfDll;
		ret.load_size = mi.SizeOfImage;

		GetModuleFileNameEx(process, module, temp, sizeof(temp));
		ret.image_name = temp;
		GetModuleBaseName(process, module, temp, sizeof(temp));
		ret.module_name = temp;
		SymLoadModule64(process, nullptr, ret.image_name.c_str(), ret.module_name.c_str(), (DWORD64)ret.base_address, ret.load_size);
		return ret;
	}
};

int symbol_callback(void *data, uintptr_t pc, const char *filename, int lineno, const char *function) {
	CrashHandlerData *ch_data = reinterpret_cast<CrashHandlerData *>(data);
	uint64_t offset = (uint64_t)ch_data->base;
	String mod_name = "main";
	if (ch_data->sym_ok) {
		IMAGEHLP_MODULE64 mod_info;
		memset(&mod_info, 0, sizeof(IMAGEHLP_MODULE64));
		mod_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
		if (SymGetModuleInfo64(ch_data->process, ch_data->pc, &mod_info)) {
			offset = mod_info.BaseOfImage;
			if (offset != (uint64_t)ch_data->base) {
				if (mod_info.ImageName[0] != 0) {
					mod_name = String((const char *)mod_info.ImageName).to_lower().get_file();
				} else if (mod_info.ModuleName[0] != 0) {
					mod_name = String((const char *)mod_info.ModuleName).to_lower();
				} else {
					mod_name = "<unknown module>";
				}
			}
		}
	}

	if (function) {
		char fname[1024];
		snprintf(fname, 1024, "%s", function);

		if (function[0] == '_') {
			int status;
			char *demangled = abi::__cxa_demangle(function, nullptr, nullptr, &status);

			if (status == 0 && demangled) {
				snprintf(fname, 1024, "%s", demangled);
			}

			if (demangled) {
				free(demangled);
			}
		}
		print_error(vformat("[%d] %x (%s+%x) - %s (%s:%d)", ch_data->index++, ch_data->pc, mod_name, ch_data->pc - offset, String::utf8(fname), String::utf8(filename), lineno));
	} else if ((int64_t)ch_data->pc > 0) {
		print_error(vformat("[%d] %x (%s+%x) - ???", ch_data->index++, ch_data->pc, mod_name, ch_data->pc - offset));
	}
	return 0;
}

void error_callback(void *data, const char *msg, int errnum) {
	CrashHandlerData *ch_data = reinterpret_cast<CrashHandlerData *>(data);
	if (ch_data->index == 0) {
		print_error(vformat("Error(%d): %s", errnum, String::utf8(msg)));
	} else {
		uint64_t offset = (uint64_t)ch_data->base;
		String mod_name = "main";
		if (ch_data->sym_ok) {
			IMAGEHLP_MODULE64 mod_info;
			memset(&mod_info, 0, sizeof(IMAGEHLP_MODULE64));
			mod_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
			if (SymGetModuleInfo64(ch_data->process, ch_data->pc, &mod_info)) {
				offset = mod_info.BaseOfImage;
				if (offset != (uint64_t)ch_data->base) {
					if (mod_info.ImageName[0] != 0) {
						mod_name = String((const char *)mod_info.ImageName).to_lower().get_file();
					} else if (mod_info.ModuleName[0] != 0) {
						mod_name = String((const char *)mod_info.ModuleName).to_lower();
					} else {
						mod_name = "<unknown module>";
					}
				}
			}
		}
		print_error(vformat("[%d] %x (%s+%x) - %s", ch_data->index++, ch_data->pc, mod_name, ch_data->pc - offset, String::utf8(msg)));
	}
}

int trace_callback(void *data, uintptr_t pc) {
	CrashHandlerData *ch_data = reinterpret_cast<CrashHandlerData *>(data);
	ch_data->pc = (uint64_t)pc;
	backtrace_pcinfo(ch_data->state, pc - ch_data->offset, &symbol_callback, &error_callback, data);
	return 0;
}

int64_t get_image_base(const WCHAR *p_path) {
	HANDLE file = CreateFileW(p_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return 0;
	}

	uint32_t pe_pos = 0;
	DWORD bytes_read;
	if (!SetFilePointerEx(file, {0x3c, 0}, nullptr, FILE_BEGIN) ||
			!ReadFile(file, &pe_pos, sizeof(pe_pos), &bytes_read, nullptr) ||
			bytes_read != sizeof(pe_pos)) {
		CloseHandle(file);
		return 0;
	}

	uint32_t magic = 0;
	if (!SetFilePointerEx(file, {pe_pos, 0}, nullptr, FILE_BEGIN) ||
			!ReadFile(file, &magic, sizeof(magic), &bytes_read, nullptr) ||
			bytes_read != sizeof(magic) ||
			magic != 0x00004550) {
		CloseHandle(file);
		return 0;
	}

	int64_t opt_header_pos = pe_pos + sizeof(magic) + 0x14;
	uint16_t opt_header_magic = 0;
	if (!SetFilePointerEx(file, {opt_header_pos, 0}, nullptr, FILE_BEGIN) ||
			!ReadFile(file, &opt_header_magic, sizeof(opt_header_magic), &bytes_read, nullptr) ||
			bytes_read != sizeof(opt_header_magic)) {
		CloseHandle(file);
		return 0;
	}

	int64_t result = 0;
	if (opt_header_magic == 0x10B) {
		uint32_t base32 = 0;
		if (!SetFilePointerEx(file, {opt_header_pos + 0x1C, 0}, nullptr, FILE_BEGIN) ||
				!ReadFile(file, &base32, sizeof(base32), &bytes_read, nullptr) ||
				bytes_read != sizeof(base32)) {
			CloseHandle(file);
			return 0;
		}
		result = base32;
	} else if (opt_header_magic == 0x20B) {
		uint64_t base64 = 0;
		if (!SetFilePointerEx(file, {opt_header_pos + 0x18, 0}, nullptr, FILE_BEGIN) ||
				!ReadFile(file, &base64, sizeof(base64), &bytes_read, nullptr) ||
				bytes_read != sizeof(base64)) {
			CloseHandle(file);
			return 0;
		}
		result = base64;
	}

	CloseHandle(file);
	return result;
}

extern void CrashHandlerException(int signal) {
	CrashHandlerData data;

	if (OS::get_singleton() == nullptr || OS::get_singleton()->is_disable_crash_handler() || IsDebuggerPresent()) {
		return;
	}

	if (OS::get_singleton()->is_crash_handler_silent()) {
		std::_Exit(0);
	}

	// 避免使用 GLOBAL_GET，因为它可能触发 EditorSettings 访问
	String msg;

	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_CRASH);
	}

	print_error("\n================================================================");
	print_error(vformat("%s: Program crashed with signal %d", __FUNCTION__, signal));

	if (String(JUNDOT_VERSION_HASH).is_empty()) {
		print_error(vformat("Engine version: %s", JUNDOT_VERSION_FULL_NAME));
	} else {
		print_error(vformat("Engine version: %s (%s)", JUNDOT_VERSION_FULL_NAME, JUNDOT_VERSION_HASH));
	}
	print_error(vformat("Dumping the backtrace. %s", msg));

	WCHAR execpath[MAX_PATH] = {0};
	if (!GetModuleFileNameW(nullptr, execpath, MAX_PATH)) {
		print_error("Failed to get executable path");
		return;
	}

	MODULEINFO mi;
	GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &mi, sizeof(mi));
	int64_t image_mem_base = reinterpret_cast<int64_t>(mi.lpBaseOfDll);
	int64_t image_file_base = get_image_base(execpath);
	data.offset = image_mem_base - image_file_base;

	std::vector<module_data> modules;
	DWORD cbNeeded;
	std::vector<HMODULE> module_handles(1);

	data.process = GetCurrentProcess();

	// 获取 exe 所在目录作为 DbgHelp 符号搜索路径（使用 W 版本匹配已存在的宽字符串）
	WCHAR sym_search_path[MAX_PATH] = L".";
	WCHAR *last_sep = wcsrchr(execpath, L'\\');
	if (last_sep) {
		size_t dir_len = last_sep - execpath;
		wcsncpy_s(sym_search_path, MAX_PATH, execpath, dir_len);
		sym_search_path[dir_len] = L'\0';
	}

	data.sym_ok = SymInitializeW(data.process, sym_search_path, false);

	if (data.sym_ok) {
		SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
		EnumProcessModules(data.process, &module_handles[0], module_handles.size() * sizeof(HMODULE), &cbNeeded);
		module_handles.resize(cbNeeded / sizeof(HMODULE));
		EnumProcessModules(data.process, &module_handles[0], module_handles.size() * sizeof(HMODULE), &cbNeeded);
		std::transform(module_handles.begin(), module_handles.end(), std::back_inserter(modules), get_mod_info(data.process));
		data.base = (uint64_t)modules[0].base_address;
	}

	print_error(vformat("Load address: %x\n", (uint64_t)data.offset));

	WCHAR debug_path[MAX_PATH * 2];
	swprintf_s(debug_path, L"%s.debugsymbols", execpath);
	const WCHAR *backtrace_path = execpath;
	if (GetFileAttributesW(debug_path) != INVALID_FILE_ATTRIBUTES) {
		backtrace_path = debug_path;
	}

	data.state = backtrace_create_state((const char *)backtrace_path, 0, &error_callback, reinterpret_cast<void *>(&data));
	if (data.state != nullptr) {
		data.index = 1;
		backtrace_simple(data.state, 1, &trace_callback, &error_callback, reinterpret_cast<void *>(&data));
	}

	print_error("-- END OF C++ BACKTRACE --");
	print_error("================================================================");

	if (data.sym_ok) {
		SymCleanup(data.process);
	}

	for (const Ref<ScriptBacktrace> &backtrace : ScriptServer::capture_script_backtraces(false)) {
		if (!backtrace->is_empty()) {
			print_error(backtrace->format());
			print_error(vformat("-- END OF %s BACKTRACE --", backtrace->get_language_name().to_upper()));
			print_error("================================================================");
		}
	}
}
#endif

CrashHandler::CrashHandler() {
	disabled = false;
}

CrashHandler::~CrashHandler() {
}

void CrashHandler::disable() {
	if (disabled) {
		return;
	}

#if defined(CRASH_HANDLER_EXCEPTION)
	signal(SIGSEGV, nullptr);
	signal(SIGFPE, nullptr);
	signal(SIGILL, nullptr);
#endif

	disabled = true;
}

void CrashHandler::initialize() {
#if defined(CRASH_HANDLER_EXCEPTION)
	signal(SIGSEGV, CrashHandlerException);
	signal(SIGFPE, CrashHandlerException);
	signal(SIGILL, CrashHandlerException);
#endif
}
