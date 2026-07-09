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

    bool IsAsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

    bool IsAsciiAlnum(wchar_t c)
    {
        return IsAsciiDigit(c) || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    }

    // AC-standard drawing filename: 10 digits + '-' + 1 alphanumeric + 1-2
    // trailing digits, i.e. 13 or 14 characters total (e.g. "1903001122-01",
    // "1830045236-M1"). Any character outside those positions/rules — an
    // underscore, a wrong-length part number, letters in the leading 10
    // digits — fails the corresponding condition below.
    struct FilenameCheck
    {
        bool lengthOk          = false;  // overall length is 13 or 14
        bool leadingDigitsOk   = false;  // first 10 characters are digits
        bool hyphenOk          = false;  // 11th character is '-'
        bool alnumOk           = false;  // 12th character is alphanumeric
        bool trailingDigitsOk  = false;  // remaining 1-2 characters are digits
    };

    FilenameCheck ValidateAcFilename(const std::wstring& name)
    {
        FilenameCheck check;
        const size_t len = name.size();

        check.lengthOk = (len == 13 || len == 14);

        if (len >= 10)
            check.leadingDigitsOk = std::all_of(name.begin(), name.begin() + 10, IsAsciiDigit);

        if (len >= 11)
            check.hyphenOk = (name[10] == L'-');

        if (len >= 12)
            check.alnumOk = IsAsciiAlnum(name[11]);

        if (check.lengthOk && len >= 12)
            check.trailingDigitsOk = std::all_of(name.begin() + 12, name.end(), IsAsciiDigit);

        return check;
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

    ProMdlName mdlName{};
    if (g_api->ProMdlMdlnameGet(mdl, mdlName) != PRO_TK_NO_ERROR)
    {
        result.elements.push_back({ "Could not read active document's filename", false });
        return result;
    }

    const std::wstring fileName(mdlName);
    const std::string  fileNameNarrow = NarrowFromWide(fileName);
    const FilenameCheck check = ValidateAcFilename(fileName);

    result.elements.push_back({ "" + fileNameNarrow + "", (check.lengthOk && check.leadingDigitsOk && check.hyphenOk && check.alnumOk && check.trailingDigitsOk) });
    //result.elements.push_back({ "Filename \"" + fileNameNarrow + "\" is 13 or 14 characters long", check.lengthOk });
    //result.elements.push_back({ "First 10 characters are numeric digits", check.leadingDigitsOk });
    //result.elements.push_back({ "11th character is a hyphen (-)", check.hyphenOk });
    //result.elements.push_back({ "12th character is alphanumeric", check.alnumOk });
    //result.elements.push_back({ "Trailing 1-2 characters are numeric digits", check.trailingDigitsOk });

    // AC-standard filename check: every positional condition above must hold.
    result.matchAny = false;
    result.passed = std::all_of(result.elements.begin(), result.elements.end(),
        [](const ElementResult& e) { return e.isInside; });

    // TESTING ONLY — exercises the PopUp flow end-to-end. Yes keeps the rule
    // passed, No puts it in the Failed section (ShowYesNoPopUp's answer
    // replaces RuleStatus on the backend). Remove before shipping this rule.
    //result.popUp.show    = true;
    //result.popUp.kind    = "YesNo";
    //result.popUp.message = "Was this manually verified as acceptable?";

    return result;
}
