#include "pch.h"
#include "CreoPlugin.h"

// CreoRuleContract.h  — shared with CreoRuleChecker (the host plugin).
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

    bool PointInBorder(double x, double y,
                        double minX, double minY,
                        double maxX, double maxY)
    {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    bool RectInBorder(double rMinX, double rMinY,
                       double rMaxX, double rMaxY,
                       double bMinX, double bMinY,
                       double bMaxX, double bMaxY)
    {
        return rMinX >= bMinX && rMaxX <= bMaxX
            && rMinY >= bMinY && rMaxY <= bMaxY;
    }

    // The view's own name in Creo (e.g. "FRONT", "SECTION_A"), the same name
    // shown by right-click > Properties on the view.
    std::wstring GetViewName(ProDrawing drawing, ProView view)
    {
        ProName name{};
        if (g_api->ProDrawingViewNameGet(drawing, view, name) != PRO_TK_NO_ERROR)
            return std::wstring();
        return std::wstring(name);
    }

    // The dimension's symbolic name (e.g. "d12", or "add912" if the user has
    // renamed it in Creo via Tools > Parameters / rename-dimension). This is
    // the identifier Creo itself uses for the dimension.
    //
    // Note: ProDimensionSymtextGet returns the dimension's *displayed value
    // text* (e.g. "12.50"), not its name — using it here was the bug that
    // caused custom dimension names to never appear.
    std::wstring GetDimensionSymbolText(ProDimension* dim)
    {
        ProName symbol{};
        if (g_api->ProDimensionSymbolGet(dim, symbol) != PRO_TK_NO_ERROR)
            return std::wstring();
        return std::wstring(symbol);
    }

    struct NoteCheck
    {
        std::wstring text;                // the note's rendered text, used as its name
        bool         isBalloon = false;   // true if a leader is anchored to an assembly component
        bool         isInside  = true;    // fully within the sheet format border
    };

    // Checks a note's line envelopes and leader attachment points against the
    // sheet border, collects its rendered text (for naming), and detects
    // whether it is a BOM balloon rather than a plain/leader note: a balloon's
    // leader is anchored to a specific assembly component (comp path with
    // table_num > 0), while ordinary notes attach to top-level geometry or
    // nowhere at all. There is no dedicated ProToolkit query for existing
    // balloons — ProBomballoon.h only exposes creation/cleanup calls — so a
    // balloon is read back the same way any other note is.
    NoteCheck InspectNote(ProDtlnote* note,
                          double bMinX, double bMinY, double bMaxX, double bMaxY)
    {
        NoteCheck check;

        for (int line = 1; ; ++line)
        {
            ProVector env[4];
            if (g_api->ProDtlnoteLineEnvelopeGet(note, line, env) != PRO_TK_NO_ERROR)
                break;
            for (int c = 0; c < 4; ++c)
                if (!PointInBorder(env[c][0], env[c][1], bMinX, bMinY, bMaxX, bMaxY))
                    check.isInside = false;
        }

        ProDtlnotedata notedata = nullptr;
        if (g_api->ProDtlnoteDataGet(note, nullptr, PRODISPMODE_NUMERIC, &notedata) != PRO_TK_NO_ERROR
            || !notedata)
            return check;

        ProDtlnoteline* lines = nullptr;
        if (g_api->ProDtlnotedataLinesCollect(notedata, &lines) == PRO_TK_NO_ERROR && lines)
        {
            int lineCount = 0;
            g_api->ProArraySizeGet((ProArray)lines, &lineCount);
            for (int li = 0; li < lineCount; ++li)
            {
                ProDtlnotetext* texts = nullptr;
                if (g_api->ProDtlnotelineTextsCollect(lines[li], &texts) == PRO_TK_NO_ERROR && texts)
                {
                    int textCount = 0;
                    g_api->ProArraySizeGet((ProArray)texts, &textCount);
                    for (int ti = 0; ti < textCount; ++ti)
                    {
                        ProLine buf{};
                        if (g_api->ProDtlnotetextStringGet(texts[ti], buf) == PRO_TK_NO_ERROR
                            && buf[0] != L'\0')
                        {
                            if (!check.text.empty())
                                check.text += L" ";
                            check.text += buf;
                        }
                    }
                    g_api->ProArrayFree((ProArray*)&texts);
                }
            }
            g_api->ProArrayFree((ProArray*)&lines);
        }

        ProDtlattach* leaders = nullptr;
        if (g_api->ProDtlnotedataLeadersCollect(notedata, &leaders) == PRO_TK_NO_ERROR && leaders)
        {
            int count = 0;
            g_api->ProArraySizeGet((ProArray)leaders, &count);
            for (int i = 0; i < count; ++i)
            {
                ProDtlattachType atype;
                ProView          aview;
                ProVector        aloc;
                ProSelection     asel;
                if (g_api->ProDtlattachGet(leaders[i], &atype, &aview, aloc, &asel) != PRO_TK_NO_ERROR)
                    continue;

                if (!PointInBorder(aloc[0], aloc[1], bMinX, bMinY, bMaxX, bMaxY))
                    check.isInside = false;

                ProAsmcomppath compPath{};
                if (g_api->ProSelectionAsmcomppathGet(asel, &compPath) == PRO_TK_NO_ERROR
                    && compPath.table_num > 0)
                    check.isBalloon = true;
            }
            g_api->ProArrayFree((ProArray*)&leaders);
        }

        g_api->ProDtlnotedataFree(notedata);
        return check;
    }
}

// ── Drawing-view visitor ────────────────────────────────────────────────────────

namespace
{
    struct ViewVisitData
    {
        ProDrawing                  drawing;
        int                         targetSheet;
        double                      bMinX, bMinY, bMaxX, bMaxY;
        std::vector<ElementResult>* out;
        int                         index;
    };

    ProError ViewFilterCb(ProDrawing drawing, ProView view, ProAppData data)
    {
        auto* d = static_cast<ViewVisitData*>(data);
        int sheet = 0;
        if (g_api->ProDrawingViewSheetGet(drawing, view, &sheet) != PRO_TK_NO_ERROR)
            return PRO_TK_CONTINUE;
        return (sheet == d->targetSheet) ? PRO_TK_NO_ERROR : PRO_TK_CONTINUE;
    }

    ProError ViewVisitCb(ProDrawing drawing, ProView view,
                         ProError /*filterStatus*/, ProAppData data)
    {
        auto* d = static_cast<ViewVisitData*>(data);

        ElementResult r;
        const std::wstring name = GetViewName(drawing, view);
        r.label = !name.empty() ? NarrowFromWide(name) : ("DrawingView " + std::to_string(d->index));
        ++d->index;
        r.isInside = false;

        ProPoint3d outline[2];
        if (g_api->ProDrawingViewOutlineGet(drawing, view, outline) == PRO_TK_NO_ERROR)
        {
            double vMinX = std::min(outline[0][0], outline[1][0]);
            double vMaxX = std::max(outline[0][0], outline[1][0]);
            double vMinY = std::min(outline[0][1], outline[1][1]);
            double vMaxY = std::max(outline[0][1], outline[1][1]);
            r.isInside = RectInBorder(vMinX, vMinY, vMaxX, vMaxY,
                                       d->bMinX, d->bMinY, d->bMaxX, d->bMaxY);
        }

        d->out->push_back(r);
        return PRO_TK_NO_ERROR;
    }
}

// ── Dimension visitor ────────────────────────────────────────────────────────────

namespace
{
    struct DimVisitData
    {
        ProDrawing                  drawing;
        int                         targetSheet;
        double                      bMinX, bMinY, bMaxX, bMaxY;
        std::vector<ElementResult>* out;
        int                         index;
    };

    ProError DimFilterCb(ProDimension* dim, ProAppData data)
    {
        auto* d = static_cast<DimVisitData*>(data);
        ProView view;
        if (g_api->ProDrawingDimensionViewGet(d->drawing, dim, &view) != PRO_TK_NO_ERROR)
            return PRO_TK_CONTINUE;
        int sheet = 0;
        if (g_api->ProDrawingViewSheetGet(d->drawing, view, &sheet) != PRO_TK_NO_ERROR)
            return PRO_TK_CONTINUE;
        return (sheet == d->targetSheet) ? PRO_TK_NO_ERROR : PRO_TK_CONTINUE;
    }

    ProError DimVisitCb(ProDimension* dim, ProError /*filterStatus*/, ProAppData data)
    {
        auto* d = static_cast<DimVisitData*>(data);

        ElementResult r;
        const std::wstring symtext = GetDimensionSymbolText(dim);
        r.label = !symtext.empty() ? NarrowFromWide(symtext) : ("Dimension " + std::to_string(d->index));
        ++d->index;
        r.isInside = false;

        ProView view;
        if (g_api->ProDrawingDimensionViewGet(d->drawing, dim, &view) != PRO_TK_NO_ERROR)
        {
            d->out->push_back(r);
            return PRO_TK_NO_ERROR;
        }

        ProDimlocation loc = nullptr;
        if (g_api->ProDimensionLocationGet(dim, view, d->drawing, &loc) != PRO_TK_NO_ERROR || !loc)
        {
            d->out->push_back(r);
            return PRO_TK_NO_ERROR;
        }

        r.isInside = true;

        {
            ProBoolean hasElbow;
            ProPoint3d textPt;
            double     elbowLen;
            if (g_api->ProDimlocationTextGet(loc, &hasElbow, textPt, &elbowLen) == PRO_TK_NO_ERROR)
                if (!PointInBorder(textPt[0], textPt[1],
                                   d->bMinX, d->bMinY, d->bMaxX, d->bMaxY))
                    r.isInside = false;
        }
        if (r.isInside)
        {
            ProPoint3d arr1, arr2;
            if (g_api->ProDimlocationArrowsGet(loc, arr1, arr2) == PRO_TK_NO_ERROR)
                if (!PointInBorder(arr1[0], arr1[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY) ||
                    !PointInBorder(arr2[0], arr2[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY))
                    r.isInside = false;
        }
        if (r.isInside)
        {
            ProPoint3d wl1, wl2;
            if (g_api->ProDimlocationWitnesslinesGet(loc, wl1, wl2) == PRO_TK_NO_ERROR)
                if (!PointInBorder(wl1[0], wl1[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY) ||
                    !PointInBorder(wl2[0], wl2[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY))
                    r.isInside = false;
        }

        g_api->ProDimlocationFree(loc);
        d->out->push_back(r);
        return PRO_TK_NO_ERROR;
    }
}

// ── Public entry point ────────────────────────────────────────────────────────
//
// Rule: every drawing view, note/balloon, and dimension on the active sheet
// must be fully contained within the sheet format border. The rule passes
// only when ALL checked entities pass; a single entity outside the border
// fails the whole rule. No user-facing popup is shown.

RuleCheckResult CreoPlugin::RuleFunctions()
{
    RuleCheckResult result;
    result.passed   = false;
    result.matchAny = false;   // pass only if every entity passes (all_of)

    if (!g_api)
        return result;   // CreoInit was not called — should never happen in normal operation

    // 1. Verify the active model is a drawing
    ProMdl mdl = nullptr;
    if (g_api->ProMdlCurrentGet(&mdl) != PRO_TK_NO_ERROR || !mdl)
        return result;

    ProMdlType mdlType;
    if (g_api->ProMdlTypeGet(mdl, &mdlType) != PRO_TK_NO_ERROR || mdlType != PRO_MDL_DRAWING)
        return result;

    ProDrawing drawing = (ProDrawing)mdl;

    // 2. Get the active sheet number
    int sheet = 0;
    if (g_api->ProDrawingCurrentSheetGet(drawing, &sheet) != PRO_TK_NO_ERROR)
        return result;

    // 3. Determine border extents
    ProPlotPaperSize paperSize;
    double           borderW = 0.0, borderH = 0.0;

    if (g_api->ProDrawingFormatSizeGet(drawing, sheet, &paperSize, &borderW, &borderH) != PRO_TK_NO_ERROR
        || borderW <= 0.0 || borderH <= 0.0)
    {
        if (g_api->ProDrawingSheetSizeGet(drawing, sheet, &paperSize, &borderW, &borderH) != PRO_TK_NO_ERROR
            || borderW <= 0.0 || borderH <= 0.0)
            return result;
    }

    const double bMinX = 0.0, bMinY = 0.0;
    const double bMaxX = borderW, bMaxY = borderH;

    // 4. Check drawing views
    {
        ViewVisitData vd{ drawing, sheet, bMinX, bMinY, bMaxX, bMaxY, &result.elements, 1 };
        g_api->ProDrawingViewVisit(drawing, ViewVisitCb, ViewFilterCb, &vd);
    }

    // 5. Check notes (including BOM balloons and leader notes)
    {
        ProDtlnote* notes = nullptr;
        if (g_api->ProDrawingDtlnotesCollect(drawing, nullptr, sheet, &notes) == PRO_TK_NO_ERROR
            && notes)
        {
            int count = 0;
            g_api->ProArraySizeGet((ProArray)notes, &count);
            for (int i = 0; i < count; ++i)
            {
                const NoteCheck check = InspectNote(&notes[i], bMinX, bMinY, bMaxX, bMaxY);
                const char* kind = check.isBalloon ? "Balloon" : "Note";

                ElementResult r;
                r.label = !check.text.empty()
                    ? NarrowFromWide(check.text)
                    : (std::string(kind) + " " + std::to_string(i + 1));
                r.isInside = check.isInside;
                result.elements.push_back(r);
            }
            g_api->ProArrayFree((ProArray*)&notes);
        }
    }

    // 6. Check dimensions
    {
        DimVisitData dd{ drawing, sheet, bMinX, bMinY, bMaxX, bMaxY, &result.elements, 1 };
        g_api->ProDrawingDimensionVisit(drawing, PRO_DIMENSION,     DimVisitCb, DimFilterCb, &dd);
        g_api->ProDrawingDimensionVisit(drawing, PRO_REF_DIMENSION, DimVisitCb, DimFilterCb, &dd);
    }

    // 7. Rule passes only when every entity is inside the border
    result.passed = !result.elements.empty() &&
        std::all_of(result.elements.begin(), result.elements.end(),
                    [](const ElementResult& e) { return e.isInside; });

    return result;
}
