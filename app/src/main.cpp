/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017  Matt Keeter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <QApplication>
#include <QSurfaceFormat>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#endif

#include "fieldes/app.hpp"
#include "fieldes/documentation.hpp"
#include "fieldes/result.hpp"

using namespace FielDes;

#ifdef _WIN32
namespace {

// What a crash leaves behind: a text file in %LOCALAPPDATA%\FielDes\FielDes\crash with the exception, the thread, and the places the
// program was at (module + offset, read with the .map files that go with the build), so that a crash that happens on one machine can be
// found on another.  It writes what it can and lets Windows go on with its own report
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info)
{
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    wchar_t dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n + 40 >= MAX_PATH) return EXCEPTION_CONTINUE_SEARCH;
    static const wchar_t* tail[] = {L"\\FielDes", L"\\FielDes", L"\\crash"};
    size_t len = n;
    for (const wchar_t* part : tail)
    {
        wcscpy_s(dir + len, MAX_PATH - len, part);
        len += wcslen(part);
        CreateDirectoryW(dir, nullptr);
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\crash-%04d%02d%02d-%02d%02d%02d.txt", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w") != 0 || !f) return EXCEPTION_CONTINUE_SEARCH;
    EXCEPTION_RECORD* rec = info->ExceptionRecord;
    std::fprintf(f, "FielDes crash %04d-%02d-%02d %02d:%02d:%02d\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::fprintf(f, "exception 0x%08lX at %p, thread %lu\n", rec->ExceptionCode, rec->ExceptionAddress,
                 GetCurrentThreadId());
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        std::fprintf(f, "  access violation: %s address %p\n", rec->ExceptionInformation[0] == 1 ? "writing" : rec->ExceptionInformation[0] == 8 ? "executing" : "reading",
                     reinterpret_cast<void*>(rec->ExceptionInformation[1]));
    auto where = [&](DWORD64 address, const char* what) {
        HMODULE module = nullptr;
        wchar_t file[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(address), &module))
            GetModuleFileNameW(module, file, MAX_PATH);
        const wchar_t* name = wcsrchr(file, L'\\');
        std::fprintf(f, "  %-6s %p  %ls+0x%llX\n", what, reinterpret_cast<void*>(address), name ? name + 1 : file,
                     static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    };
#if defined(_M_X64)
    CONTEXT context = *info->ContextRecord;
    for (int frame = 0; frame < 48; ++frame)
    {
        where(context.Rip, frame == 0 ? "at" : "from");
        DWORD64 base = 0;
        auto* entry = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (!entry)
        {
            // (a place that is no code of any module -- a call through a pointer that is not one: the caller left its return address on
            // the top of the stack, and everything below that is read from the stack as a list of addresses that are code)
            if (frame == 0)
            {
                const DWORD64 stack = context.Rsp;
                int found = 0;
                for (int k = 0; k < 4096 && found < 24; ++k)
                {
                    DWORD64 value = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(stack + 8 * k), &value, sizeof value, nullptr)) break;
                    HMODULE module = nullptr;
                    if (value > 0x10000 && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                              reinterpret_cast<LPCWSTR>(value), &module))
                    {
                        std::fprintf(f, "  stack[%d]:\n", k);
                        where(value, "code");
                        ++found;
                    }
                }
            }
            break;
        }
        void* handlerData = nullptr;
        DWORD64 frameBase = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, entry, &context, &handlerData, &frameBase, nullptr);
        if (!context.Rip) break;
    }
#else
    where(reinterpret_cast<DWORD64>(rec->ExceptionAddress), "at");
#endif
    std::fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}

}   // anonymous namespace
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashFilter);
#endif
    {   // Configure default OpenGL as 3.2 Core
        QSurfaceFormat format;
        format.setVersion(3, 2);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setSamples(8);
        QSurfaceFormat::setDefaultFormat(format);
    }

    // Register metatypes to be sent between threads
    qRegisterMetaType<Result>("Result");
    qRegisterMetaType<Documentation>("Documentation");

    QCoreApplication::setOrganizationName("FielDes");
    QCoreApplication::setApplicationName("FielDes");
    QCoreApplication::setApplicationVersion(FIELDES_VERSION);

    App a(argc, argv);
    a.exec();
}
