// Test helper: on an access violation (or any fatal exception) print the faulting address and a
// symbolised stack trace, then exit with code 3. Windows only; a no-op elsewhere.
// Usage: call mc_install_crash_handler() at the top of main(). Link dbghelp (mc_test does).
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

static LONG WINAPI mc_crash_filter(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED && code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(stderr, "\n*** fatal exception 0x%08lx at %p", code, ep->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        std::fprintf(stderr, " (%s address %p)", ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                     (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    std::fprintf(stderr, "\n");
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf = {};
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
#ifdef _M_X64
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset = ctx.Rip; sf.AddrFrame.Offset = ctx.Rbp; sf.AddrStack.Offset = ctx.Rsp;
#else
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    sf.AddrPC.Offset = ctx.Eip; sf.AddrFrame.Offset = ctx.Ebp; sf.AddrStack.Offset = ctx.Esp;
#endif
    char symbuf[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 511;
    for (int i = 0; i < 40; i++) {
        if (!StackWalk64(machine, proc, GetCurrentThread(), &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        if (!sf.AddrPC.Offset) break;
        DWORD64 disp = 0;
        const char *name = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        IMAGEHLP_LINE64 line = {}; line.SizeOfStruct = sizeof line; DWORD ld = 0;
        if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ld, &line))
            std::fprintf(stderr, "  #%d %s+0x%llx  %s:%lu\n", i, name, (unsigned long long)disp, line.FileName, line.LineNumber);
        else
            std::fprintf(stderr, "  #%d %s+0x%llx\n", i, name, (unsigned long long)disp);
    }
    std::fflush(stderr);
    ExitProcess(3);
}

static inline void mc_install_crash_handler() { AddVectoredExceptionHandler(1, mc_crash_filter); }
#else
static inline void mc_install_crash_handler() {}
#endif
