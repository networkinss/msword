#include "opus_x64_compat.h"

#include <cstdio>
#include <cstring>

namespace
{
char g_last_report[512] = {};
}

extern "C" void OpusX64ReportSz(const char *message, const char *file,
		int line)
	{
	_snprintf_s(g_last_report, sizeof(g_last_report), _TRUNCATE,
			"%s (%s:%d)", message != nullptr ? message : "startup failure",
			file != nullptr ? file : "unknown", line);
	OutputDebugStringA("VintageWord x64: ");
	OutputDebugStringA(g_last_report);
	OutputDebugStringA("\r\n");
	}

extern "C" void OpusX64ReportWin32(const char *message, DWORD error,
		const char *file, int line)
	{
	char system_message[256] = {};
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error, 0, system_message,
			static_cast<DWORD>(sizeof(system_message)), nullptr);
	for (char *pch = system_message; *pch != '\0'; ++pch)
		if (*pch == '\r' || *pch == '\n')
			*pch = ' ';
	_snprintf_s(g_last_report, sizeof(g_last_report), _TRUNCATE,
			"%s: Win32 error %lu (%s) (%s:%d)",
			message != nullptr ? message : "startup failure", error,
			system_message[0] != '\0' ? system_message : "no system text",
			file != nullptr ? file : "unknown", line);
	OutputDebugStringA("VintageWord x64: ");
	OutputDebugStringA(g_last_report);
	OutputDebugStringA("\r\n");
	}

extern "C" const char *OpusX64LastReport(void)
	{
	return g_last_report;
	}

extern "C" void OpusX64Trace(const char *message)
	{
	char module_path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, module_path, MAX_PATH);
	char *file_part = std::strrchr(module_path, '\\');
	if (file_part != nullptr)
		{
		*file_part = '\0';
		char *bin_part = std::strrchr(module_path, '\\');
		if (bin_part != nullptr)
			*bin_part = '\0';
		}
	char trace_path[MAX_PATH] = {};
	_snprintf_s(trace_path, sizeof(trace_path), _TRUNCATE,
			"%s\\build\\vintageword-port-trace.txt", module_path);
	HANDLE file = CreateFileA(trace_path, FILE_APPEND_DATA, FILE_SHARE_READ,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;
	DWORD written = 0;
	const char *text = message != nullptr ? message : "(null)";
	WriteFile(file, text, (DWORD)std::strlen(text), &written, nullptr);
	WriteFile(file, "\r\n", 2, &written, nullptr);
	CloseHandle(file);
	}
