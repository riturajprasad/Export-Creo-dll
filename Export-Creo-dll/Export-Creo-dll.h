// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the EXPORTCREODLL_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// EXPORTCREODLL_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef EXPORTCREODLL_EXPORTS
#define EXPORTCREODLL_API __declspec(dllexport)
#else
#define EXPORTCREODLL_API __declspec(dllimport)
#endif

// This class is exported from the dll
class EXPORTCREODLL_API CExportCreodll {
public:
	CExportCreodll(void);
	// TODO: add your methods here.
};

extern EXPORTCREODLL_API int nExportCreodll;

EXPORTCREODLL_API int fnExportCreodll(void);
