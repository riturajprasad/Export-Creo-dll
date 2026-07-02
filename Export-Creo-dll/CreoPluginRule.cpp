#include "pch.h"
#include "CreoPlugin.h"

// CreoRuleContract.h  ← shared with CreoRuleChecker (the host plugin).
// Defines CreoApiContext: the table of ProToolkit function pointers that the
// host passes via CreoInit() before calling CreoExecuteRule().
// This DLL does NOT link protk_dll*.lib / ucore.lib / udata.lib — all
// ProToolkit calls are made through the function pointers below.
#include "CreoRuleContract.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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
    // A 2D symbol counts as a "burrs/sharp-edges" note if its rendered text
    // contains either phrase (case-insensitive, substring match).
    const wchar_t* const kRequiredPhrases[] = {
        L"Burrs removed notes",
        L"Burrs and Sharp edges removed",
        L"Difference with the drawing, mentioned in box \"Compare\" or \"Replaces\"",
    };

    bool ContainsPhrase(const std::wstring& haystack, const wchar_t* needle)
    {
        const std::wstring n(needle);
        auto it = std::search(haystack.begin(), haystack.end(), n.begin(), n.end(),
            [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
        return it != haystack.end();
    }

    bool MatchesRequiredPhrase(const std::wstring& text)
    {
        for (const wchar_t* phrase : kRequiredPhrases)
            if (ContainsPhrase(text, phrase))
                return true;
        return false;
    }

    std::string NarrowFromWide(const std::wstring& wide)
    {
        if (wide.empty()) return std::string();
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return std::string();
        std::string out(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
        return out;
    }

    // Looks up the name the symbol was given in Creo's symbol library
    // (e.g. "BURR_NOTE"), the same name shown by Insert > Symbol in the drawing.
    std::wstring GetSymbolDefName(ProDtlsymdef* definition)
    {
        ProDtlsymdefdata symdefdata = nullptr;
        if (g_api->ProDtlsymdefDataGet(definition, &symdefdata) != PRO_TK_NO_ERROR || !symdefdata)
            return std::wstring();

        ProName name{};
        std::wstring result;
        if (g_api->ProDtlsymdefdataNameGet(symdefdata, name) == PRO_TK_NO_ERROR)
            result = name;

        return result;
    }

    // Checks the fixed text baked into a 2D symbol's definition (the note
    // lines drawn as part of the symbol artwork) against the required phrases.
    bool DefinitionTextMatches(ProDrawing drawing, int sheet, ProDtlsymdef* definition)
    {
        bool matched = false;

        ProDtlnote* notes = nullptr;
        if (g_api->ProDrawingDtlnotesCollect(drawing, definition, sheet, &notes) != PRO_TK_NO_ERROR || !notes)
            return false;

        int noteCount = 0;
        g_api->ProArraySizeGet(notes, &noteCount);

        for (int n = 0; n < noteCount && !matched; ++n)
        {
            ProDtlnotedata notedata = nullptr;
            if (g_api->ProDtlnoteDataGet(&notes[n], definition, PRODISPMODE_NUMERIC, &notedata) != PRO_TK_NO_ERROR || !notedata)
                continue;

            ProDtlnoteline* lines = nullptr;
            if (g_api->ProDtlnotedataLinesCollect(notedata, &lines) == PRO_TK_NO_ERROR && lines)
            {
                int lineCount = 0;
                g_api->ProArraySizeGet(lines, &lineCount);

                for (int l = 0; l < lineCount && !matched; ++l)
                {
                    ProDtlnotetext* texts = nullptr;
                    if (g_api->ProDtlnotelineTextsCollect(lines[l], &texts) == PRO_TK_NO_ERROR && texts)
                    {
                        int textCount = 0;
                        g_api->ProArraySizeGet(texts, &textCount);

                        for (int t = 0; t < textCount && !matched; ++t)
                        {
                            ProLine buf{};
                            if (g_api->ProDtlnotetextStringGet(texts[t], buf) == PRO_TK_NO_ERROR)
                                matched = MatchesRequiredPhrase(std::wstring(buf));
                        }

                        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&texts));
                    }
                }

                g_api->ProArrayFree(reinterpret_cast<ProArray*>(&lines));
            }

            g_api->ProDtlnotedataFree(notedata);
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&notes));
        return matched;
    }

    // Checks the per-instance variable text (the values a designer typed in
    // when placing the symbol) against the required phrases.
    bool VartextMatches(ProDtlsyminstdata data)
    {
        bool matched = false;

        ProDtlvartext* vartexts = nullptr;
        if (g_api->ProDtlsyminstdataVartextsCollect(data, &vartexts) != PRO_TK_NO_ERROR || !vartexts)
            return false;

        int vtCount = 0;
        g_api->ProArraySizeGet(vartexts, &vtCount);

        for (int v = 0; v < vtCount && !matched; ++v)
        {
            ProLine prompt{};
            ProLine value{};
            if (g_api->ProDtlvartextDataGet(vartexts[v], prompt, value) == PRO_TK_NO_ERROR)
                matched = MatchesRequiredPhrase(std::wstring(value));
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&vartexts));
        return matched;
    }

    struct SymbolCheck
    {
        std::wstring name;   // the symbol's library name, e.g. "BURR_NOTE"
        bool         matched;
    };

    // A 2D-symbol instance passes if EITHER its symbol-definition artwork
    // text OR its per-instance variable text carries one of the required notes.
    SymbolCheck SymbolInstanceHasRequiredText(ProDrawing drawing, int sheet, ProDtlsyminst* syminst)
    {
        SymbolCheck check{ std::wstring(), false };

        ProDtlsyminstdata data = nullptr;
        if (g_api->ProDtlsyminstDataGet(syminst, PRODISPMODE_NUMERIC, &data) != PRO_TK_NO_ERROR || !data)
            return check;

        ProDtlsymdef definition{};
        if (g_api->ProDtlsyminstdataDefGet(data, &definition) == PRO_TK_NO_ERROR)
        {
            check.name    = GetSymbolDefName(&definition);
            check.matched = DefinitionTextMatches(drawing, sheet, &definition);
        }

        if (!check.matched)
            check.matched = VartextMatches(data);

        g_api->ProDtlsyminstdataFree(data);
        return check;
    }
}

// ── Public entry point ────────────────────────────────────────────────────────
//
// Rule: pass when at least one 2D symbol on the active drawing carries the
// text "Burrs removed notes" or "Burrs and Sharp edges removed"; fail when no
// 2D symbol contains either phrase (including when the drawing has none).

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

    int sheetCount = 0;
    if (g_api->ProDrawingSheetsCount(drawing, &sheetCount) != PRO_TK_NO_ERROR || sheetCount <= 0)
    {
        result.elements.push_back({ "Drawing has no sheets", false });
        return result;
    }

    int symbolIndex = 0;
    for (int sheet = 1; sheet <= sheetCount; ++sheet)
    {
        ProDtlsyminst* syminsts = nullptr;
        if (g_api->ProDrawingDtlsyminstsCollect(drawing, sheet, &syminsts) != PRO_TK_NO_ERROR || !syminsts)
            continue;

        int count = 0;
        g_api->ProArraySizeGet(syminsts, &count);

        for (int i = 0; i < count; ++i)
        {
            ++symbolIndex;
            const SymbolCheck check = SymbolInstanceHasRequiredText(drawing, sheet, &syminsts[i]);

            const std::string label = !check.name.empty()
                ? NarrowFromWide(check.name)
                : ("2D Symbol " + std::to_string(symbolIndex) + " (Sheet " + std::to_string(sheet) + ")");

            result.elements.push_back({ label, check.matched });
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&syminsts));
    }

    if (result.elements.empty())
        result.elements.push_back({ "No 2D symbols found on drawing", false });

    // Rule passes as soon as one 2D symbol carries either required note.
    result.passed = std::any_of(result.elements.begin(), result.elements.end(),
                                 [](const ElementResult& e) { return e.isInside; });

    return result;
}
