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
    // A note counts as a "burrs/sharp-edges" note if its rendered text
    // contains either phrase (case-insensitive, substring match).
    const wchar_t* const kRequiredPhrases[] = {
        L"Burrs removed notes",
        L"Burrs and Sharp edges removed",
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

    // True if `note` is a cell of any drawing table (BOM, hole table, ...) —
    // such notes are excluded from the phrase check entirely.
    bool NoteIsTableCell(ProDtlnote* note, ProDwgtable* tables, int tableCount)
    {
        int row = 0, col = 0;
        for (int t = 0; t < tableCount; ++t)
            if (g_api->ProDtlnoteTableCellGet(note, &tables[t], &row, &col) == PRO_TK_NO_ERROR)
                return true;
        return false;
    }

    // Classification of a note used only to choose its result-label prefix —
    // mirrors the BL/MD/SY/LD/NT categories the border-check rule uses, minus
    // isTableCell (table-cell notes are filtered out before this runs) and
    // the border/sheet fields that rule needs and this one does not.
    struct NoteInfo
    {
        std::wstring text;                  // the note's rendered text, concatenated line by line
        bool         isBalloon     = false; // true if a leader is anchored to an assembly component
        bool         isModelDriven = false; // mirrors a note authored in the 3D model, not the sheet
        bool         hasSymbol     = false; // has a detail symbol instance embedded in its text
        bool         hasLeader     = false; // has at least one leader (balloon or attached to geometry)
    };

    // Collects a note's rendered text (concatenated line by line, the same
    // text a user reads on the sheet) and classifies it the same way the
    // border-check rule does, so multi-line notes can be searched for the
    // required phrases as a single substring check while still labelling
    // balloons/model-notes/symbol-notes/leader-notes distinctly.
    NoteInfo InspectNote(ProDtlnote* note)
    {
        NoteInfo info;

        {
            ProMdl refModel = nullptr;
            if (g_api->ProDtlnoteModelrefGet(note, nullptr, 1, 1, &refModel) == PRO_TK_NO_ERROR && refModel)
                info.isModelDriven = true;
        }

        {
            ProDtlsyminst* syminsts = nullptr;
            if (g_api->ProDtlnoteDtlsyminstsCollect(note, &syminsts) == PRO_TK_NO_ERROR && syminsts)
            {
                int symCount = 0;
                g_api->ProArraySizeGet(reinterpret_cast<ProArray>(syminsts), &symCount);
                info.hasSymbol = symCount > 0;
                g_api->ProArrayFree(reinterpret_cast<ProArray*>(&syminsts));
            }
        }

        ProDtlnotedata notedata = nullptr;
        if (g_api->ProDtlnoteDataGet(note, nullptr, PRODISPMODE_NUMERIC, &notedata) != PRO_TK_NO_ERROR || !notedata)
            return info;

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
                            if (!info.text.empty())
                                info.text += L" ";
                            info.text += buf;
                        }
                    }

                    g_api->ProArrayFree(reinterpret_cast<ProArray*>(&texts));
                }
            }

            g_api->ProArrayFree(reinterpret_cast<ProArray*>(&lines));
        }

        ProDtlattach* leaders = nullptr;
        if (g_api->ProDtlnotedataLeadersCollect(notedata, &leaders) == PRO_TK_NO_ERROR && leaders)
        {
            int count = 0;
            g_api->ProArraySizeGet(reinterpret_cast<ProArray>(leaders), &count);
            info.hasLeader = count > 0;

            for (int i = 0; i < count; ++i)
            {
                ProDtlattachType atype;
                ProView          aview;
                ProVector        aloc;
                ProSelection     asel;
                if (g_api->ProDtlattachGet(leaders[i], &atype, &aview, aloc, &asel) != PRO_TK_NO_ERROR)
                    continue;

                // A balloon's leader is anchored to a specific assembly
                // component (comp path with table_num > 0); ordinary leader
                // notes attach to top-level geometry or nowhere at all.
                ProAsmcomppath compPath{};
                if (g_api->ProSelectionAsmcomppathGet(asel, &compPath) == PRO_TK_NO_ERROR
                    && compPath.table_num > 0)
                    info.isBalloon = true;
            }

            g_api->ProArrayFree(reinterpret_cast<ProArray*>(&leaders));
        }

        g_api->ProDtlnotedataFree(notedata);
        return info;
    }

    // Prefixes the note's text with a category tag, the same BL/MD/SY/LD/NT
    // scheme the border-check rule uses, so the two rules' results read the
    // same way in the UI.
    std::string LabelForNote(const NoteInfo& info)
    {
        if (info.text.empty())
            return std::string();

        const std::string text = NarrowFromWide(info.text);

        if (info.isBalloon)
            return "BL: " + text;   // BL for "Balloon Note"
        if (info.isModelDriven)
            return "MD: " + text;   // MD for "Model Note"
        if (info.hasSymbol)
            return "SY: " + text;   // SY for "Symbol Note"
        if (info.hasLeader)
            return "LD: " + text;   // LD for "Leader Note"
        return "NT: " + text;       // NT for "Note"
    }
}

// ── Public entry point ────────────────────────────────────────────────────────
//
// Rule: pass when at least one note on the active sheet (of any kind except
// table-cell notes) carries the text "Burrs removed notes" or "Burrs and
// Sharp edges removed" anywhere in its rendered text, even as a substring of
// a longer sentence; fail when no such note is found (including when the
// drawing has none).

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

    // Table-cell notes (BOM rows, hole tables, ...) are never checked, so
    // every note must first be tested against every table on the sheet.
    ProDwgtable* tables = nullptr;
    int tableCount = 0;
    if (g_api->ProDrawingTablesCollect(drawing, &tables) == PRO_TK_NO_ERROR && tables)
        g_api->ProArraySizeGet(tables, &tableCount);

    ProDtlnote* notes = nullptr;
    if (g_api->ProDrawingDtlnotesCollect(drawing, nullptr, activeSheet, &notes) == PRO_TK_NO_ERROR && notes)
    {
        int count = 0;
        g_api->ProArraySizeGet(notes, &count);

        for (int i = 0; i < count; ++i)
        {
            if (NoteIsTableCell(&notes[i], tables, tableCount))
                continue;   // excluded per rule — table-cell notes are never checked

            const NoteInfo info    = InspectNote(&notes[i]);
            const bool     matched = MatchesRequiredPhrase(info.text);
            const std::string label = LabelForNote(info);

            result.elements.push_back({ label, matched });
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&notes));
    }

    if (tables)
        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&tables));

    if (result.elements.empty())
        result.elements.push_back({ "No notes found on drawing", false });

    // Rule passes as soon as one note carries either required phrase.
    result.matchAny = true;
    result.passed = std::any_of(result.elements.begin(), result.elements.end(),
                                 [](const ElementResult& e) { return e.isPass; });

    return result;
}
