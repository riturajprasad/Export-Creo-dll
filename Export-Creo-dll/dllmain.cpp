// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// ProToolkit DLL libs (protk_dllmd_NU.lib / protk_dll_NU.lib) require these
// two callbacks to be defined by the application. This DLL is not a standalone
// ProToolkit plugin, so empty stubs are sufficient to satisfy the linker.
extern "C"
{
    int  user_initialize() { return 0; }  // 0 == PRO_TK_NO_ERROR
    void user_terminate()  {}
}

