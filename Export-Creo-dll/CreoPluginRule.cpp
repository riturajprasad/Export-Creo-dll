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

    // Strips leading/trailing whitespace only (no case change, no interior
    // space removal) — used so a cell holding just "Oil"/"Water" plus
    // incidental padding still compares equal.
    std::wstring TrimW(const std::wstring& text)
    {
        const size_t start = text.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) return std::wstring();
        const size_t end = text.find_last_not_of(L" \t\r\n");
        return text.substr(start, end - start + 1);
    }

    // Strips every whitespace character, per the rule's "ignore all spaces
    // and normalize text" requirement for the Note-vs-code comparison.
    std::wstring StripSpaces(const std::wstring& text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (wchar_t c : text)
            if (!std::iswspace(static_cast<wint_t>(c)))
                out.push_back(c);
        return out;
    }

    // Concatenates every line of a table cell into one string (cells can
    // hold multiple lines of text). Returns an empty string for an empty
    // or nonexistent cell.
    std::wstring GetCellText(ProDwgtable* table, int column, int row)
    {
        ProWstring* lines = nullptr;
        if (g_api->ProDwgtableCelltextGet(table, column, row, PRODWGTABLE_NORMAL, &lines) != PRO_TK_NO_ERROR || !lines)
            return std::wstring();

        int count = 0;
        g_api->ProArraySizeGet(lines, &count);

        std::wstring text;
        for (int i = 0; i < count; ++i)
        {
            if (!lines[i]) continue;
            if (!text.empty()) text += L" ";
            text += lines[i];
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&lines));
        return text;
    }

    // Concatenates every rendered text line of a note into one block of
    // text, the same text a user reads on the sheet.
    std::wstring CollectNoteText(ProDtlnote* note)
    {
        std::wstring result;

        ProDtlnotedata notedata = nullptr;
        if (g_api->ProDtlnoteDataGet(note, nullptr, PRODISPMODE_NUMERIC, &notedata) != PRO_TK_NO_ERROR || !notedata)
            return result;

        ProDtlnoteline* lines = nullptr;
        if (g_api->ProDtlnotedataLinesCollect(notedata, &lines) == PRO_TK_NO_ERROR && lines)
        {
            int lineCount = 0;
            g_api->ProArraySizeGet(lines, &lineCount);

            for (int l = 0; l < lineCount; ++l)
            {
                ProDtlnotetext* texts = nullptr;
                if (g_api->ProDtlnotelineTextsCollect(lines[l], &texts) == PRO_TK_NO_ERROR && texts)
                {
                    int textCount = 0;
                    g_api->ProArraySizeGet(texts, &textCount);

                    for (int t = 0; t < textCount; ++t)
                    {
                        ProLine buf{};
                        if (g_api->ProDtlnotetextStringGet(texts[t], buf) == PRO_TK_NO_ERROR && buf[0] != L'\0')
                        {
                            if (!result.empty()) result += L" ";
                            result += buf;
                        }
                    }

                    g_api->ProArrayFree(reinterpret_cast<ProArray*>(&texts));
                }
            }

            g_api->ProArrayFree(reinterpret_cast<ProArray*>(&lines));
        }

        g_api->ProDtlnotedataFree(notedata);
        return result;
    }
}

// ── Public entry point ────────────────────────────────────────────────────────
//
// Rule: scan every drawing-table cell for one whose entire (trimmed) text is
// exactly "Oil" or exactly "Water" (case-sensitive, not a substring match).
// If neither is found, the rule passes trivially. If "Oil" is found, some Note on the
// active sheet must contain "9822 1260 55" (spaces ignored) as a substring;
// if "Water" is found, some Note must likewise contain "9822 1261 73". Each
// keyword that is present is checked independently and ANY that are present
// must be satisfied (any_of) for the rule to pass.

RuleCheckResult CreoPlugin::RuleFunctions()
{
    RuleCheckResult result;
    result.passed = false;
    result.matchAny = true;   // any_of: keyword present ("Oil"/"Water") must be satisfied
    const std::string noneFoundLabel = "No Oil or Water found";
    std::string oilCellLabel;
    std::string waterCellLabel;

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

    // Step 1: scan every table cell on the drawing for "Oil" / "Water".
    bool oilFound = false;
    bool waterFound = false;

    ProDwgtable* tables = nullptr;
    if (g_api->ProDrawingTablesCollect(drawing, &tables) == PRO_TK_NO_ERROR && tables)
    {
        int tableCount = 0;
        g_api->ProArraySizeGet(tables, &tableCount);

        for (int t = 0; t < tableCount && (!oilFound || !waterFound); ++t)
        {
            ProDwgtable* table = &tables[t];

            int rowCount = 0;
            int colCount = 0;
            if (g_api->ProDwgtableRowsCount(table, &rowCount) != PRO_TK_NO_ERROR || rowCount <= 0)
                continue;
            if (g_api->ProDwgtableColumnsCount(table, &colCount) != PRO_TK_NO_ERROR || colCount <= 0)
                continue;

            for (int row = 1; row <= rowCount && (!oilFound || !waterFound); ++row)
            {
                for (int col = 1; col <= colCount && (!oilFound || !waterFound); ++col)
                {
                    const std::wstring cellText = TrimW(GetCellText(table, col, row));
                    if (cellText.empty()) continue;

                    // Exact match against the whole cell — "Oil"/"oil" and
                    // "Water"/"water" are the only accepted casings; any
                    // other casing, or either word as a substring of other
                    // text, does not count.
                    if (!oilFound && (cellText == L"Oil" || cellText == L"oil"))
                    {
                        oilCellLabel = NarrowFromWide(cellText);
                        oilFound = true;
                    }
                    if (!waterFound && (cellText == L"Water" || cellText == L"water"))
                    {
                        waterCellLabel = NarrowFromWide(cellText);
                        waterFound = true;
                    }
                }
            }
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&tables));
    }

    // Step 2: if neither keyword is present in any table cell, the rule
    // passes trivially and reports a single informational entry.
    if (!oilFound && !waterFound)
    {
        result.elements.push_back({ noneFoundLabel, true });
    }
    else
    {
        // Step 3: "Oil"/"Water" was found — collect every Note's text on the
        // active sheet and check for the respective required code, ignoring
        // spaces on both sides of the comparison.
        bool oilCodeFound = false;
        bool waterCodeFound = false;

        ProDtlnote* notes = nullptr;
        if (g_api->ProDrawingDtlnotesCollect(drawing, nullptr, activeSheet, &notes) == PRO_TK_NO_ERROR && notes)
        {
            int noteCount = 0;
            g_api->ProArraySizeGet(notes, &noteCount);

            for (int i = 0; i < noteCount && (!oilCodeFound || !waterCodeFound); ++i)
            {
                const std::wstring noteTextNoSpace = StripSpaces(CollectNoteText(&notes[i]));
                if (noteTextNoSpace.empty()) continue;

                if (!oilCodeFound && noteTextNoSpace.find(L"9822126055") != std::wstring::npos)
                    oilCodeFound = true;
                if (!waterCodeFound && noteTextNoSpace.find(L"9822126173") != std::wstring::npos)
                    waterCodeFound = true;
            }

            g_api->ProArrayFree(reinterpret_cast<ProArray*>(&notes));
        }

        // Step 4: one element per keyword that was actually present in a table cell.
        if (oilFound)
            result.elements.push_back({ "TC: " + oilCellLabel, oilCodeFound });
        if (waterFound)
            result.elements.push_back({ "TC: " + waterCellLabel, waterCodeFound });
    }

    if (result.elements.empty())
        result.elements.push_back({ "Value not found", false });

    result.passed = std::any_of(result.elements.begin(), result.elements.end(),
        [](const ElementResult& e) { return e.isPass; });

    return result;
}
