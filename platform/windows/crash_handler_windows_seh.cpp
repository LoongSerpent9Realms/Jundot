/**************************************************************************/
/*  crash_handler_windows_seh.cpp                                         */
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

// Forward declarations for crash diagnostics.
static void write_native_crash_report(EXCEPTION_POINTERS *ep);
static bool launch_crash_dialog_if_present();

// Backtrace code based on: https://stackoverflow.com/questions/6205981/windows-c-stack-trace-from-a-running-app

#include <psapi.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#pragma pack(push, before_imagehlp, 8)
#include <imagehlp.h>
#pragma pack(pop, before_imagehlp)

struct module_data {
	std::string image_name;
	std::string module_name;
	void *base_address = nullptr;
	DWORD load_size;
};

class symbol {
	typedef IMAGEHLP_SYMBOL64 sym_type;
	sym_type *sym;
	static const int max_name_len = 1024;

public:
	symbol(HANDLE process, DWORD64 address) :
			sym((sym_type *)::operator new(sizeof(*sym) + max_name_len)) {
		memset(sym, '\0', sizeof(*sym) + max_name_len);
		sym->SizeOfStruct = sizeof(*sym);
		sym->MaxNameLength = max_name_len;
		DWORD64 displacement;

		SymGetSymFromAddr64(process, address, &displacement, sym);
	}

	std::string name() { return std::string(sym->Name); }
	std::string undecorated_name() {
		if (*sym->Name == '\0') {
			return "<couldn't map PC to fn name>";
		}
		std::vector<char> und_name(max_name_len);
		UnDecorateSymbolName(sym->Name, &und_name[0], max_name_len, UNDNAME_COMPLETE);
		return std::string(&und_name[0], strlen(&und_name[0]));
	}
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

DWORD CrashHandlerException(EXCEPTION_POINTERS *ep) {
	HANDLE process = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();
	DWORD offset_from_symbol = 0;
	IMAGEHLP_LINE64 line = {};
	std::vector<module_data> modules;
	DWORD cbNeeded;
	std::vector<HMODULE> module_handles(1);

	if (OS::get_singleton() == nullptr || OS::get_singleton()->is_disable_crash_handler() || IsDebuggerPresent()) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (OS::get_singleton()->is_crash_handler_silent()) {
		std::_Exit(0);
	}

	write_native_crash_report(ep);

	// 避免使用 GLOBAL_GET，因为它可能触发 EditorSettings 访问
	String msg;

	// Tell MainLoop about the crash. This can be handled by users too in Node.
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_CRASH);
	}

	print_error("\n================================================================");
	print_error(vformat("%s: Program crashed", __FUNCTION__));

	// Print the engine version just before, so that people are reminded to include the version in backtrace reports.
	if (String(JUNDOT_VERSION_HASH).is_empty()) {
		print_error(vformat("Engine version: %s", JUNDOT_VERSION_FULL_NAME));
	} else {
		print_error(vformat("Engine version: %s (%s)", JUNDOT_VERSION_FULL_NAME, JUNDOT_VERSION_HASH));
	}
	print_error(vformat("Dumping the backtrace. %s", msg));

	// 获取 exe 所在目录作为 DbgHelp 符号搜索路径（使用 ANSI 版本与 SymInitialize 匹配）
	char execpath[MAX_PATH] = {0};
	char sym_search_path[MAX_PATH] = ".";
	if (GetModuleFileNameA(nullptr, execpath, MAX_PATH)) {
		char *last_sep = strrchr(execpath, '\\');
		if (last_sep) {
			size_t dir_len = last_sep - execpath;
			strncpy_s(sym_search_path, MAX_PATH, execpath, dir_len);
			sym_search_path[dir_len] = '\0';
		}
	}

	// Load the symbols:
	if (!SymInitialize(process, sym_search_path, false)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	EnumProcessModules(process, &module_handles[0], module_handles.size() * sizeof(HMODULE), &cbNeeded);
	module_handles.resize(cbNeeded / sizeof(HMODULE));
	EnumProcessModules(process, &module_handles[0], module_handles.size() * sizeof(HMODULE), &cbNeeded);
	std::transform(module_handles.begin(), module_handles.end(), std::back_inserter(modules), get_mod_info(process));
	void *base = modules[0].base_address;

	print_error(vformat("Load address: %x\n", (uint64_t)base));

	// Setup stuff:
	CONTEXT *context = ep->ContextRecord;
	STACKFRAME64 frame;
	bool skip_first = false;

	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrStack.Mode = AddrModeFlat;
	frame.AddrFrame.Mode = AddrModeFlat;

#if defined(_M_X64)
	frame.AddrPC.Offset = context->Rip;
	frame.AddrStack.Offset = context->Rsp;
	frame.AddrFrame.Offset = context->Rbp;
#elif defined(_M_ARM64) || defined(_M_ARM64EC)
	frame.AddrPC.Offset = context->Pc;
	frame.AddrStack.Offset = context->Sp;
	frame.AddrFrame.Offset = context->Fp;
#elif defined(_M_ARM)
	frame.AddrPC.Offset = context->Pc;
	frame.AddrStack.Offset = context->Sp;
	frame.AddrFrame.Offset = context->R11;
#else
	frame.AddrPC.Offset = context->Eip;
	frame.AddrStack.Offset = context->Esp;
	frame.AddrFrame.Offset = context->Ebp;

	// Skip the first one to avoid a duplicate on 32-bit mode
	skip_first = true;
#endif

	line.SizeOfStruct = sizeof(line);
	IMAGE_NT_HEADERS *h = ImageNtHeader(base);
	DWORD image_type = h->FileHeader.Machine;

	int n = 0;
	do {
		if (skip_first) {
			skip_first = false;
		} else {
			if (frame.AddrPC.Offset != 0) {
				std::string fnName = symbol(process, frame.AddrPC.Offset).undecorated_name();

				IMAGEHLP_MODULE64 mod_info;
				memset(&mod_info, 0, sizeof(IMAGEHLP_MODULE64));
				mod_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
				uint64_t offset = (uint64_t)base;
				String mod_name = "main";
				if (SymGetModuleInfo64(process, frame.AddrPC.Offset, &mod_info)) {
					offset = mod_info.BaseOfImage;
					if (offset != (uint64_t)base) {
						if (mod_info.ImageName[0] != 0) {
							mod_name = String((const char *)mod_info.ImageName).to_lower().get_file();
						} else if (mod_info.ModuleName[0] != 0) {
							mod_name = String((const char *)mod_info.ModuleName).to_lower();
						} else {
							mod_name = "<unknown module>";
						}
					}
				}
				if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &offset_from_symbol, &line)) {
					print_error(vformat("[%d] %x (%s+%x) - %s (%s:%d)", n, (uint64_t)frame.AddrPC.Offset, mod_name, (uint64_t)frame.AddrPC.Offset - offset, fnName.c_str(), (char *)line.FileName, (int)line.LineNumber));
				} else if (!fnName.empty()) {
					print_error(vformat("[%d] %x (%s+%x) - %s", n, (uint64_t)frame.AddrPC.Offset, mod_name, (uint64_t)frame.AddrPC.Offset - offset, fnName.c_str()));
				} else {
					print_error(vformat("[%d] %x (%s+%x) - ???", n, (uint64_t)frame.AddrPC.Offset, mod_name, (uint64_t)frame.AddrPC.Offset - offset));
				}
			}

			n++;
		}

		if (!StackWalk64(image_type, process, hThread, &frame, context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
			break;
		}
	} while (frame.AddrReturn.Offset != 0 && n < 256);

	print_error("-- END OF C++ BACKTRACE --");
	print_error("================================================================");

	SymCleanup(process);

	for (const Ref<ScriptBacktrace> &backtrace : ScriptServer::capture_script_backtraces(false)) {
		if (!backtrace->is_empty()) {
			print_error(backtrace->format());
			print_error(vformat("-- END OF %s BACKTRACE --", backtrace->get_language_name().to_upper()));
			print_error("================================================================");
		}
	}

	// Launch the crash dialog before terminating.
	launch_crash_dialog_if_present();

	// Pass the exception to the OS
	return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * Launches CrashDialog.exe if present in the engine directory or Tools/CrashDialog/.
 * Uses DETACHED_PROCESS so the dialog survives the parent crash.
 * Crash info is written to a temp file and passed via --crash-info argument.
 * 
 * Note: This function must be robust and avoid operations that could cause
 * a secondary crash (e.g., memory allocation, complex string operations).
 */
static void write_native_crash_report(EXCEPTION_POINTERS *ep) {
	WCHAR engine_path[MAX_PATH] = { 0 };
	if (!GetModuleFileNameW(nullptr, engine_path, MAX_PATH)) {
		return;
	}

	WCHAR engine_dir[MAX_PATH] = { 0 };
	WCHAR *last_sep = wcsrchr(engine_path, L'\\');
	if (!last_sep) {
		return;
	}
	size_t dir_len = last_sep - engine_path;
	wcsncpy_s(engine_dir, MAX_PATH, engine_path, dir_len);
	engine_dir[dir_len] = L'\0';

	WCHAR log_dir[MAX_PATH * 2] = { 0 };
	swprintf_s(log_dir, L"%s\\logs", engine_dir);
	CreateDirectoryW(log_dir, nullptr);

	SYSTEMTIME st;
	GetLocalTime(&st);

	WCHAR report_path[MAX_PATH * 2] = { 0 };
	swprintf_s(report_path, L"%s\\jundot-native-crash-%04d%02d%02d-%02d%02d%02d-%lu.txt",
			log_dir,
			st.wYear,
			st.wMonth,
			st.wDay,
			st.wHour,
			st.wMinute,
			st.wSecond,
			GetCurrentProcessId());

	FILE *report_file = nullptr;
	if (_wfopen_s(&report_file, report_path, L"w, ccs=UTF-8") != 0 || !report_file) {
		return;
	}

	fwprintf_s(report_file, L"========== Jundot Native Crash ==========\n");
	fwprintf_s(report_file, L"Time: %04d-%02d-%02d %02d:%02d:%02d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	fwprintf_s(report_file, L"Engine: %s\n", engine_path);
	fwprintf_s(report_file, L"EngineDir: %s\n", engine_dir);
	fwprintf_s(report_file, L"Version: %hs\n", JUNDOT_VERSION_FULL_NAME);
	const char *hash = JUNDOT_VERSION_HASH;
	if (hash && hash[0] != '\0') {
		fwprintf_s(report_file, L"Hash: %hs\n", hash);
	}
	if (ep && ep->ExceptionRecord) {
		fwprintf_s(report_file, L"ExceptionCode: 0x%08lX\n", ep->ExceptionRecord->ExceptionCode);
		fwprintf_s(report_file, L"ExceptionAddress: 0x%p\n", ep->ExceptionRecord->ExceptionAddress);
	}
	fwprintf_s(report_file, L"\nThe full C++ backtrace is still printed through the engine logger. This emergency file is written before the crash dialog starts so there is always an on-disk breadcrumb.\n");
	fclose(report_file);
}

static bool launch_crash_dialog_if_present() {
	WCHAR engine_path[MAX_PATH] = {0};
	if (!GetModuleFileNameW(nullptr, engine_path, MAX_PATH)) {
		return false;
	}

	WCHAR engine_dir[MAX_PATH] = {0};
	WCHAR* last_sep = wcsrchr(engine_path, L'\\');
	if (!last_sep) {
		return false;
	}
	size_t dir_len = last_sep - engine_path;
	wcsncpy_s(engine_dir, MAX_PATH, engine_path, dir_len);
	engine_dir[dir_len] = L'\0';

	WCHAR crash_dialog_path[MAX_PATH * 2] = {0};
	WCHAR test_path[MAX_PATH * 2];
	bool found = false;

	swprintf_s(test_path, L"%s\\..\\Tools\\CrashDialog\\JundotCrashDialog.exe", engine_dir);
	if (GetFileAttributesW(test_path) != INVALID_FILE_ATTRIBUTES) {
		wcscpy_s(crash_dialog_path, test_path);
		found = true;
	}

	if (!found) {
		swprintf_s(test_path, L"%s\\Tools\\CrashDialog\\JundotCrashDialog.exe", engine_dir);
		if (GetFileAttributesW(test_path) != INVALID_FILE_ATTRIBUTES) {
			wcscpy_s(crash_dialog_path, test_path);
			found = true;
		}
	}

	if (!found) {
		swprintf_s(test_path, L"%s\\CrashDialog\\JundotCrashDialog.exe", engine_dir);
		if (GetFileAttributesW(test_path) != INVALID_FILE_ATTRIBUTES) {
			wcscpy_s(crash_dialog_path, test_path);
			found = true;
		}
	}

	if (!found) {
		swprintf_s(test_path, L"%s\\JundotCrashDialog.exe", engine_dir);
		if (GetFileAttributesW(test_path) != INVALID_FILE_ATTRIBUTES) {
			wcscpy_s(crash_dialog_path, test_path);
			found = true;
		}
	}

	if (!found) {
		return false;
	}

	WCHAR crash_info_path[MAX_PATH];
	WCHAR temp_path[MAX_PATH];
	if (GetTempPathW(MAX_PATH, temp_path) == 0) {
		return false;
	}
	swprintf_s(crash_info_path, L"%s\\jundot_crash_info_%llu.txt",
			temp_path,
			(unsigned long long)GetCurrentThreadId());

	FILE* crash_info_file = nullptr;
	if (_wfopen_s(&crash_info_file, crash_info_path, L"w") != 0 || !crash_info_file) {
		return false;
	}

	fwprintf_s(crash_info_file, L"Engine: %s\n", engine_path);
	fwprintf_s(crash_info_file, L"EngineDir: %s\n", engine_dir);
	fwprintf_s(crash_info_file, L"Version: %hs\n", JUNDOT_VERSION_FULL_NAME);
	const char *hash = JUNDOT_VERSION_HASH;
	if (hash && hash[0] != '\0') {
		fwprintf_s(crash_info_file, L"Hash: %hs\n", hash);
	}
	SYSTEMTIME st;
	GetSystemTime(&st);
	fwprintf_s(crash_info_file, L"CrashTime: %04d-%02d-%02d %02d:%02d:%02d\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	fclose(crash_info_file);

	WCHAR cmd_line[MAX_PATH * 3];
	swprintf_s(cmd_line, L"\"%s\" --crash-info \"%s\"", crash_dialog_path, crash_info_path);

	STARTUPINFOW si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(si);

	BOOL created = CreateProcessW(
			crash_dialog_path,
			cmd_line,
			nullptr,
			nullptr,
			FALSE,
			DETACHED_PROCESS,
			nullptr,
			engine_dir,
			&si,
			&pi);

	if (created) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return true;
	} else {
		DeleteFileW(crash_info_path);
		return false;
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

	disabled = true;
}

void CrashHandler::initialize() {
}
