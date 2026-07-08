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
#include <cstdio>
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

    // Most drawing-entity position queries (view outlines, note envelopes/
    // leader points, dimension text/arrow/witness-line points) return
    // coordinates in Creo's "screen" coordinate system, not the physical
    // "drawing" coordinate system the sheet border is measured in (origin at
    // the sheet's bottom-left corner, in drawing units). ProDrawingSheetTrfGet
    // supplies the matrix that converts one to the other; every raw point
    // must be run through this before it can be compared to the border.
    void ScreenToDrawing(const ProMatrix& trf, const double in[3], double& outX, double& outY)
    {
        outX = in[0] * trf[0][0] + in[1] * trf[1][0] + in[2] * trf[2][0] + trf[3][0];
        outY = in[0] * trf[0][1] + in[1] * trf[1][1] + in[2] * trf[2][1] + trf[3][1];
    }

    bool PointInBorderScreen(const ProMatrix& trf, const double in[3],
                              double bMinX, double bMinY, double bMaxX, double bMaxY)
    {
        double x, y;
        ScreenToDrawing(trf, in, x, y);
        return PointInBorder(x, y, bMinX, bMinY, bMaxX, bMaxY);
    }

    // ── TEMPORARY DIAGNOSTIC LOGGING ────────────────────────────────────────
    // Writes raw vs. transformed coordinates to %TEMP%\CreoPluginRule_debug.log
    // so real numbers from a live Creo session can be compared against the
    // sheet border, instead of guessing the screen->drawing transform blind.
    // Remove this whole block once the containment checks are confirmed correct.
    void DebugLog(const std::string& line)
    {
        wchar_t tempPath[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, tempPath) == 0)
            return;
        std::wstring path = std::wstring(tempPath) + L"CreoPluginRule_debug.log";
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"a") == 0 && f)
        {
            fputs(line.c_str(), f);
            fputs("\n", f);
            fclose(f);
        }
    }

    std::string PtStr(const double p[3])
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)", p[0], p[1], p[2]);
        return buf;
    }

    std::string XyStr(double x, double y)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%.4f, %.4f)", x, y);
        return buf;
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
        std::wstring text;                 // the note's rendered text, used as its name
        bool         isBalloon     = false; // true if a leader is anchored to an assembly component
        bool         isInside      = true;  // fully within the sheet format border
        bool         onActiveSheet = true;  // false only if a view attachment proves otherwise
        bool         isTableCell   = false; // belongs to a drawing table cell (BOM row, hole table, ...)
        bool         isModelDriven = false; // mirrors a note authored in the 3D model, not the sheet
        bool         hasSymbol     = false; // has a detail symbol instance embedded in its text
        bool         hasLeader     = false; // has at least one leader (balloon or attached to geometry)
    };

    // Checks a note's line envelopes and leader attachment points against the
    // sheet border, collects its rendered text (for naming), and detects
    // whether it is a BOM balloon rather than a plain/leader note: a balloon's
    // leader is anchored to a specific assembly component (comp path with
    // table_num > 0), while ordinary notes attach to top-level geometry or
    // nowhere at all. There is no dedicated ProToolkit query for existing
    // balloons — ProBomballoon.h only exposes creation/cleanup calls — so a
    // balloon is read back the same way any other note is.
    NoteCheck InspectNote(ProDtlnote* note, ProDrawing drawing, int targetSheet,
                          ProDwgtable* tables, int tableCount,
                          double bMinX, double bMinY, double bMaxX, double bMaxY,
                          const ProMatrix& sheetTrf)
    {
        NoteCheck check;

        for (int t = 0; t < tableCount && !check.isTableCell; ++t)
        {
            int row = 0, col = 0;
            if (g_api->ProDtlnoteTableCellGet(note, &tables[t], &row, &col) == PRO_TK_NO_ERROR)
                check.isTableCell = true;
        }

        {
            ProMdl refModel = nullptr;
            if (g_api->ProDtlnoteModelrefGet(note, nullptr, 1, 1, &refModel) == PRO_TK_NO_ERROR
                && refModel)
                check.isModelDriven = true;
        }

        {
            ProDtlsyminst* syminsts = nullptr;
            if (g_api->ProDtlnoteDtlsyminstsCollect(note, &syminsts) == PRO_TK_NO_ERROR && syminsts)
            {
                int symCount = 0;
                g_api->ProArraySizeGet((ProArray)syminsts, &symCount);
                check.hasSymbol = symCount > 0;
                g_api->ProArrayFree((ProArray*)&syminsts);
            }
        }

        // A note's view (if it has one) unambiguously ties it to a sheet, the
        // same way ViewFilterCb/DimFilterCb pin views/dimensions to a sheet.
        // Free-floating notes (no view attachment) have no such check and are
        // trusted to be the ones ProDrawingDtlnotesCollect returned for this sheet.
        auto CheckViewSheet = [&](ProView view)
        {
            if (!view)
                return;
            int viewSheet = 0;
            if (g_api->ProDrawingViewSheetGet(drawing, view, &viewSheet) == PRO_TK_NO_ERROR
                && viewSheet != targetSheet)
                check.onActiveSheet = false;
        };

        for (int line = 1; ; ++line)
        {
            ProVector env[4];
            if (g_api->ProDtlnoteLineEnvelopeGet(note, line, env) != PRO_TK_NO_ERROR)
                break;
            for (int c = 0; c < 4; ++c)
                if (!PointInBorderScreen(sheetTrf, env[c], bMinX, bMinY, bMaxX, bMaxY))
                    check.isInside = false;
        }

        ProDtlnotedata notedata = nullptr;
        if (g_api->ProDtlnoteDataGet(note, nullptr, PRODISPMODE_NUMERIC, &notedata) != PRO_TK_NO_ERROR
            || !notedata)
            return check;

        {
            ProDtlattach mainAttach = nullptr;
            if (g_api->ProDtlnotedataAttachmentGet(notedata, &mainAttach) == PRO_TK_NO_ERROR
                && mainAttach)
            {
                ProDtlattachType atype;
                ProView          aview;
                ProVector        aloc;
                ProSelection     asel;
                if (g_api->ProDtlattachGet(mainAttach, &atype, &aview, aloc, &asel) == PRO_TK_NO_ERROR)
                    CheckViewSheet(aview);
            }
        }

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
            check.hasLeader = count > 0;
            for (int i = 0; i < count; ++i)
            {
                ProDtlattachType atype;
                ProView          aview;
                ProVector        aloc;
                ProSelection     asel;
                if (g_api->ProDtlattachGet(leaders[i], &atype, &aview, aloc, &asel) != PRO_TK_NO_ERROR)
                    continue;

                CheckViewSheet(aview);

                if (!PointInBorderScreen(sheetTrf, aloc, bMinX, bMinY, bMaxX, bMaxY))
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
        const ProMatrix*            sheetTrf;
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
            // ProDrawingViewOutlineGet returns the outline in screen
            // coordinates — convert both corners to drawing coordinates
            // before comparing against the (drawing-coordinate) sheet border.
            double p0x, p0y, p1x, p1y;
            ScreenToDrawing(*d->sheetTrf, outline[0], p0x, p0y);
            ScreenToDrawing(*d->sheetTrf, outline[1], p1x, p1y);

            double vMinX = std::min(p0x, p1x);
            double vMaxX = std::max(p0x, p1x);
            double vMinY = std::min(p0y, p1y);
            double vMaxY = std::max(p0y, p1y);
            r.isInside = RectInBorder(vMinX, vMinY, vMaxX, vMaxY,
                                       d->bMinX, d->bMinY, d->bMaxX, d->bMaxY);

            char buf[400];
            snprintf(buf, sizeof(buf),
                     "[View] %-20s raw=%s,%s -> drawing=%s,%s  border=(0,0)-(%.4f,%.4f)  inside=%d",
                     r.label.c_str(),
                     PtStr(outline[0]).c_str(), PtStr(outline[1]).c_str(),
                     XyStr(p0x, p0y).c_str(), XyStr(p1x, p1y).c_str(),
                     d->bMaxX, d->bMaxY, (int)r.isInside);
            DebugLog(buf);
        }
        else
        {
            DebugLog("[View] " + r.label + " -- ProDrawingViewOutlineGet FAILED");
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
        const ProMatrix*            sheetTrf;
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
            DebugLog("[Dim] " + r.label + " -- ProDrawingDimensionViewGet FAILED (no view)");
            d->out->push_back(r);
            return PRO_TK_NO_ERROR;
        }

        // ProDimensionLocationGet rejects a call where both view and drawing
        // are non-NULL (PRO_TK_BAD_INPUTS / PRO_TK_NOT_VALID) — when a
        // drawing is supplied, view must be NULL. `view` above is only used
        // to confirm the dimension is displayed on a view.
        ProDimlocation loc = nullptr;
        ProError locErr = g_api->ProDimensionLocationGet(dim, nullptr, d->drawing, &loc);
        if (locErr != PRO_TK_NO_ERROR || !loc)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "[Dim] %s -- ProDimensionLocationGet FAILED, err=%d", r.label.c_str(), (int)locErr);
            DebugLog(buf);
            d->out->push_back(r);
            return PRO_TK_NO_ERROR;
        }

        r.isInside = true;
        std::string logLine = "[Dim] " + r.label + ": ";

        // ProDimlocation*Get points are in screen coordinates, same as the
        // other drawing-entity position queries — convert before comparing.
        {
            ProBoolean hasElbow;
            ProPoint3d textPt;
            double     elbowLen;
            if (g_api->ProDimlocationTextGet(loc, &hasElbow, textPt, &elbowLen) == PRO_TK_NO_ERROR)
            {
                double x, y;
                ScreenToDrawing(*d->sheetTrf, textPt, x, y);
                bool ok = PointInBorder(x, y, d->bMinX, d->bMinY, d->bMaxX, d->bMaxY);
                logLine += "text raw=" + PtStr(textPt) + " -> " + XyStr(x, y) + (ok ? " [OK] " : " [OUT] ");
                if (!ok) r.isInside = false;
            }
            else
            {
                logLine += "text=N/A ";
            }
        }
        if (true)
        {
            ProPoint3d arr1, arr2;
            if (g_api->ProDimlocationArrowsGet(loc, arr1, arr2) == PRO_TK_NO_ERROR)
            {
                double x1, y1, x2, y2;
                ScreenToDrawing(*d->sheetTrf, arr1, x1, y1);
                ScreenToDrawing(*d->sheetTrf, arr2, x2, y2);
                bool ok = PointInBorder(x1, y1, d->bMinX, d->bMinY, d->bMaxX, d->bMaxY)
                       && PointInBorder(x2, y2, d->bMinX, d->bMinY, d->bMaxX, d->bMaxY);
                logLine += "arrows raw=" + PtStr(arr1) + "," + PtStr(arr2)
                         + " -> " + XyStr(x1, y1) + "," + XyStr(x2, y2) + (ok ? " [OK] " : " [OUT] ");
                if (!ok) r.isInside = false;
            }
            else
            {
                logLine += "arrows=N/A ";
            }
        }
        if (true)
        {
            ProPoint3d wl1, wl2;
            if (g_api->ProDimlocationWitnesslinesGet(loc, wl1, wl2) == PRO_TK_NO_ERROR)
            {
                double x1, y1, x2, y2;
                ScreenToDrawing(*d->sheetTrf, wl1, x1, y1);
                ScreenToDrawing(*d->sheetTrf, wl2, x2, y2);
                bool ok = PointInBorder(x1, y1, d->bMinX, d->bMinY, d->bMaxX, d->bMaxY)
                       && PointInBorder(x2, y2, d->bMinX, d->bMinY, d->bMaxX, d->bMaxY);
                logLine += "witness raw=" + PtStr(wl1) + "," + PtStr(wl2)
                         + " -> " + XyStr(x1, y1) + "," + XyStr(x2, y2) + (ok ? " [OK] " : " [OUT] ");
                if (!ok) r.isInside = false;
            }
            else
            {
                logLine += "witness=N/A ";
            }
        }

        char borderBuf[64];
        snprintf(borderBuf, sizeof(borderBuf), " border=(0,0)-(%.4f,%.4f) FINAL=%d", d->bMaxX, d->bMaxY, (int)r.isInside);
        logLine += borderBuf;
        DebugLog(logLine);

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

    // 3. Determine border extents.
    // ProDrawingSheetSizeGet returns the size "in the sheet units" — the same
    // unit system every other position in this file ends up in after the
    // screen->drawing transform. ProDrawingFormatSizeGet, by contrast, is
    // documented to always return its width/height IN INCHES regardless of
    // the drawing's actual units — on a metric (mm) drawing that silently
    // produces a border ~25x too small and every entity fails containment.
    // So SheetSizeGet is tried first; FormatSizeGet is only a last-resort
    // fallback (and may still be unit-mismatched if it's the one that succeeds).
    ProPlotPaperSize paperSize;
    double           borderW = 0.0, borderH = 0.0;
    bool             borderFromInches = false;

    if (g_api->ProDrawingSheetSizeGet(drawing, sheet, &paperSize, &borderW, &borderH) != PRO_TK_NO_ERROR
        || borderW <= 0.0 || borderH <= 0.0)
    {
        if (g_api->ProDrawingFormatSizeGet(drawing, sheet, &paperSize, &borderW, &borderH) != PRO_TK_NO_ERROR
            || borderW <= 0.0 || borderH <= 0.0)
            return result;
        borderFromInches = true;
    }

    const double bMinX = 0.0, bMinY = 0.0;
    const double bMaxX = borderW, bMaxY = borderH;

    {
        char buf[256];
        snprintf(buf, sizeof(buf), "\n=== Rule run: sheet=%d border=(0,0)-(%.4f,%.4f) [source: %s] ===",
                 sheet, borderW, borderH,
                 borderFromInches ? "ProDrawingFormatSizeGet - INCHES, may be unit-mismatched"
                                   : "ProDrawingSheetSizeGet - sheet units");
        DebugLog(buf);
    }

    // 3b. Screen-coordinate -> drawing-coordinate transform for this sheet.
    // View outlines, note envelopes/leaders, and dimension text/arrow/witness
    // points are all returned in screen coordinates, while the border above
    // is in drawing coordinates (origin at the sheet's bottom-left corner) —
    // without this conversion those points can never be compared correctly.
    ProMatrix sheetTrf;
    ProName   sheetSizeName{};
    bool haveSheetTrf = (g_api->ProDrawingSheetTrfGet(drawing, sheet, sheetSizeName, sheetTrf) == PRO_TK_NO_ERROR);
    if (!haveSheetTrf)
    {
        DebugLog("ProDrawingSheetTrfGet FAILED — falling back to identity (no screen->drawing conversion).");
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                sheetTrf[i][j] = (i == j) ? 1.0 : 0.0;
    }

    {
        char buf[256];
        for (int i = 0; i < 4; ++i)
        {
            snprintf(buf, sizeof(buf), "sheetTrf row%d = (%.6f, %.6f, %.6f, %.6f)", i,
                     sheetTrf[i][0], sheetTrf[i][1], sheetTrf[i][2], sheetTrf[i][3]);
            DebugLog(buf);
        }
    }

    // 4. Check drawing views
    {
        ViewVisitData vd{ drawing, sheet, bMinX, bMinY, bMaxX, bMaxY, &sheetTrf, &result.elements, 1 };
        g_api->ProDrawingViewVisit(drawing, ViewVisitCb, ViewFilterCb, &vd);
    }

    // 5. Check notes (including BOM balloons and leader notes)
    // Balloons are prefixed "BL <text>" so they're always distinguishable
    // from plain/leader notes in entityName; every other category falls
    // back to a category label only when it has no rendered text of its own.
    {
        ProDwgtable* tables = nullptr;
        int tableCount = 0;
        if (g_api->ProDrawingTablesCollect(drawing, &tables) == PRO_TK_NO_ERROR && tables)
            g_api->ProArraySizeGet((ProArray)tables, &tableCount);

        ProDtlnote* notes = nullptr;
        if (g_api->ProDrawingDtlnotesCollect(drawing, nullptr, sheet, &notes) == PRO_TK_NO_ERROR
            && notes)
        {
            int count = 0;
            g_api->ProArraySizeGet((ProArray)notes, &count);

            for (int i = 0; i < count; ++i)
            {
                const NoteCheck check = InspectNote(&notes[i], drawing, sheet, tables, tableCount,
                                                     bMinX, bMinY, bMaxX, bMaxY, sheetTrf);
                if (!check.onActiveSheet)
                    continue;   // belongs to a view on a different sheet — exclude it

                ElementResult r;
                if (check.isBalloon)
                    r.label = check.text.empty() ? "" : "BL: " + NarrowFromWide(check.text); // BL for "Balloon Note"
                else if (check.isTableCell)
                    r.label = check.text.empty() ? "" : "TC: " + NarrowFromWide(check.text); // TC for "Table Cell Note"
                else if (check.isModelDriven)
                    r.label = check.text.empty() ? "" : "MD: " + NarrowFromWide(check.text); // MD for "Model Note"
                else if (check.hasSymbol)
                    r.label = check.text.empty() ? "" : "SY: " + NarrowFromWide(check.text); // SY for "Symbol Note"
                else if (check.hasLeader)
                    r.label = check.text.empty() ? "" : "LD: " + NarrowFromWide(check.text); // LD for "Leader Note"
                else
                    r.label = check.text.empty() ? "" : "NT: " + NarrowFromWide(check.text); // NT for "Note"
                r.isInside = check.isInside;
                result.elements.push_back(r);
            }
            g_api->ProArrayFree((ProArray*)&notes);
        }

        if (tables)
            g_api->ProArrayFree((ProArray*)&tables);
    }

    // 6. Check dimensions
    {
        DimVisitData dd{ drawing, sheet, bMinX, bMinY, bMaxX, bMaxY, &sheetTrf, &result.elements, 1 };
        g_api->ProDrawingDimensionVisit(drawing, PRO_DIMENSION,     DimVisitCb, DimFilterCb, &dd);
        g_api->ProDrawingDimensionVisit(drawing, PRO_REF_DIMENSION, DimVisitCb, DimFilterCb, &dd);
    }

    // 7. Rule passes only when every entity is inside the border
    result.passed = !result.elements.empty() &&
        std::all_of(result.elements.begin(), result.elements.end(),
                    [](const ElementResult& e) { return e.isInside; });

    return result;
}
