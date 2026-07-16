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
#include <regex>
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
//
// Rule: for every row of the drawing's BOM-style table (the table that has
// both a "MATERIAL" and a "COMMENTS" column), the COMMENTS cell must read a
// thickness — "T" or "TH" followed by "=" and a numeric value — unless that
// row's MATERIAL cell reads "See Standard", which exempts the row entirely.
// Spacing/case are ignored on both sides of the comparison.
namespace
{
    // Concatenates every line of a table cell into one string (cells can
    // hold multiple lines of text).  Returns an empty string for an empty
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

    // Strips all whitespace and upper-cases, so "T = 4.0", "t=4.0" and
    // "T=4.0" all compare equal, per the rule's "ignore spaces, normalize
    // text" requirement.
    std::wstring NormalizeNoSpaceUpper(const std::wstring& text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (wchar_t c : text)
        {
            if (std::iswspace(static_cast<wint_t>(c))) continue;
            out.push_back(static_cast<wchar_t>(std::towupper(static_cast<wint_t>(c))));
        }
        return out;
    }

    bool IsHeaderCell(ProDwgtable* table, int column, int row, const wchar_t* headerName)
    {
        return NormalizeNoSpaceUpper(GetCellText(table, column, row)) == headerName;
    }

    // Scans a single row for the MATERIAL/COMMENTS header cells. Only writes
    // to materialCol/commentsCol/materialHeaderRow/commentsHeaderRow when it
    // finds a match, so it can be called row-by-row and accumulate results
    // without clobbering what a previous call already found. Stops scanning
    // columns as soon as both are found (or already known).
    void LocateHeadersInRow(ProDwgtable* table, int row, int colCount,
        int& materialCol, int& materialHeaderRow,
        int& commentsCol, int& commentsHeaderRow)
    {
        for (int col = 1; col <= colCount && (materialCol < 0 || commentsCol < 0); ++col)
        {
            if (materialCol < 0 && IsHeaderCell(table, col, row, L"MATERIAL"))
            {
                materialCol = col;
                materialHeaderRow = row;
            }
            if (commentsCol < 0 && IsHeaderCell(table, col, row, L"COMMENTS"))
            {
                commentsCol = col;
                commentsHeaderRow = row;
            }
        }
    }

    bool IsSeeStandard(const std::wstring& normalizedMaterial)
    {
        return normalizedMaterial == L"SEESTANDARD";
    }

    // Matches "T=<number>" or "TH=<number>" in an already-normalized
    // (no-space, upper-case) string. The "(?:^|[^A-Z])" guard keeps this
    // from matching inside an unrelated word such as "WIDTH=10".
    bool CommentHasThicknessValue(const std::wstring& normalizedComment)
    {
        static const std::wregex kPattern(LR"((?:^|[^A-Z])TH?=\d+(?:\.\d+)?)");
        return std::regex_search(normalizedComment, kPattern);
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

    ProDwgtable* tables = nullptr;
    if (g_api->ProDrawingTablesCollect(drawing, &tables) != PRO_TK_NO_ERROR || !tables)
    {
        // Fail condition: the MATERIAL/COMMENTS table cells are missing.
        result.elements.push_back({ "TC: COMMENTS", false });
        return result;
    }

    int tableCount = 0;
    g_api->ProArraySizeGet(tables, &tableCount);

    bool foundBomTable = false;
    bool overallPass   = true;

    for (int t = 0; t < tableCount; ++t)
    {
        ProDwgtable* table = &tables[t];

        int rowCount = 0;
        int colCount = 0;
        if (g_api->ProDwgtableRowsCount(table, &rowCount) != PRO_TK_NO_ERROR || rowCount <= 0)
            continue;
        if (g_api->ProDwgtableColumnsCount(table, &colCount) != PRO_TK_NO_ERROR || colCount <= 0)
            continue;

        // Locate the header cells (row/column indices are 1-based).
        int materialCol = -1, materialHeaderRow = -1;
        int commentsCol = -1, commentsHeaderRow = -1;

        // Fast path: every standard BOM table carries its MATERIAL/COMMENTS
        // headers in row 1, so one O(colCount) scan resolves the common
        // case without touching the rest of the grid.
        LocateHeadersInRow(table, 1, colCount, materialCol, materialHeaderRow, commentsCol, commentsHeaderRow);

        // Fallback: headers weren't (both) on row 1 — either this table
        // isn't a match at all, or its header sits elsewhere. Scan the
        // remaining rows as a single flattened loop (row-major index via
        // div/mod) rather than a second nested loop, stopping the moment
        // both columns are known.
        if (materialCol < 0 || commentsCol < 0)
        {
            const int remainingCells = (rowCount - 1) * colCount;
            for (int idx = 0; idx < remainingCells && (materialCol < 0 || commentsCol < 0); ++idx)
            {
                const int row = 2 + idx / colCount;
                const int col = 1 + idx % colCount;

                if (materialCol < 0 && IsHeaderCell(table, col, row, L"MATERIAL"))
                {
                    materialCol = col;
                    materialHeaderRow = row;
                }
                if (commentsCol < 0 && IsHeaderCell(table, col, row, L"COMMENTS"))
                {
                    commentsCol = col;
                    commentsHeaderRow = row;
                }
            }
        }

        if (materialCol < 0 || commentsCol < 0)
            continue;   // not the table this rule cares about

        foundBomTable = true;

        for (int row = 1; row <= rowCount; ++row)
        {
            if (row == materialHeaderRow || row == commentsHeaderRow)
                continue;

            const std::wstring materialText = GetCellText(table, materialCol, row);
            const std::wstring commentsText = GetCellText(table, commentsCol, row);

            if (materialText.empty() && commentsText.empty())
                continue;   // blank row

            if (IsSeeStandard(NormalizeNoSpaceUpper(materialText)))
                continue;   // MATERIAL is "See Standard" — row is exempt

            // Fail condition: MATERIAL is not "See Standard" but COMMENTS is missing.
            if (commentsText.empty())
            {
                overallPass = false;
                continue;
            }

            if (!CommentHasThicknessValue(NormalizeNoSpaceUpper(commentsText)))
                overallPass = false;
        }
    }

    g_api->ProArrayFree(reinterpret_cast<ProArray*>(&tables));

    if (!foundBomTable)
    {
        // Fail condition: the MATERIAL/COMMENTS table cells are missing.
        result.elements.push_back({ "TC: COMMENTS", false });
        return result;
    }

    // Only one entity is reported: the whole COMMENTS column passes only if
    // every row in it passed (or was exempted).
    result.elements.push_back({ "TC: COMMENTS", overallPass });

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
