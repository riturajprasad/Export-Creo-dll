#include "pch.h"
#include "CreoPlugin.h"

// CreoRuleContract.h  ← shared with CreoRuleChecker (the host plugin).
// Defines CreoApiContext: the table of ProToolkit function pointers that the
// host passes via CreoInit() before calling CreoExecuteRule().
// This DLL does NOT link protk_dll*.lib / ucore.lib / udata.lib — all
// ProToolkit calls are made through the function pointers below.
#include "CreoRuleContract.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

// ── ProToolkit API context ────────────────────────────────────────────────────
// Filled by the host plugin (a proper ProToolkit add-in) via CreoInit().
// Using the host's pointers bypasses the per-DLL initialisation requirement.
static const CreoApiContext* g_api = nullptr;

// CreoInit is called by the host before every CreoExecuteRule invocation.
// It is declared extern "C" so GetProcAddress finds it without name mangling.
extern "C" __declspec(dllexport)
void CreoInit(const CreoApiContext* api)
{
    g_api = api;
}

// ── Rule-specific helpers ──────────────────────────────────────────────────────
namespace
{

    std::string NarrowFromWide(const std::wstring& wide)
    {
        if (wide.empty()) return std::string();
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return std::string();
        std::string out(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
        return out;
    }

}

// ── Public entry point ────────────────────────────────────────────────────────

RuleCheckResult CreoPlugin::RuleFunctions()
{
    RuleCheckResult result;
    result.passed = false;

    if (!g_api)
        return result;   // CreoInit was not called — should never happen in normal operation

    ProMdl mdl;
    if (g_api->ProMdlCurrentGet(&mdl) != PRO_TK_NO_ERROR)
    {
        result.elements.push_back({ "No active Creo model", false });
        return result;
    }

    ProMdlType mdlType;
    if (g_api->ProMdlTypeGet(mdl, &mdlType) != PRO_TK_NO_ERROR || mdlType != PRO_MDL_DRAWING)
    {
        result.elements.push_back({ "Active model is not a drawing", false });
        return result;
    }

    ProDrawing drawing = reinterpret_cast<ProDrawing>(mdl);

    int activeSheet = 0;
    if (g_api->ProDrawingCurrentSheetGet(drawing, &activeSheet) != PRO_TK_NO_ERROR || activeSheet <= 0)
    {
        result.elements.push_back({ "Could not determine active sheet", false });
        return result;
    }

    if (result.elements.empty())
        result.elements.push_back({ "Value not found", false });

    result.matchAny = true;
    result.passed = std::any_of(result.elements.begin(), result.elements.end(),
        [](const ElementResult& e) { return e.isPass; });

    // TESTING ONLY — exercises the PopUp flow end-to-end. Yes keeps the rule
    // passed, No puts it in the Failed section (ShowYesNoPopUp's answer
    // replaces RuleStatus on the backend). Remove before shipping this rule.
    //result.popUp.show    = true;
    //result.popUp.kind    = "YesNo";
    //result.popUp.message = "Was this manually verified as acceptable?";

    return result;
}
