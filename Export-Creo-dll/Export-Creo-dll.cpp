// Export-Creo-dll.cpp : Defines the exported functions for the DLL.
//

#include "pch.h"
#include "framework.h"
#include "Export-Creo-dll.h"


// This is an example of an exported variable
EXPORTCREODLL_API int nExportCreodll=0;

// This is an example of an exported function.
EXPORTCREODLL_API int fnExportCreodll(void)
{
    return 0;
}

// This is the constructor of a class that has been exported.
CExportCreodll::CExportCreodll()
{
    return;
}
