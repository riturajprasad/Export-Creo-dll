#include "pch.h"
#include "CreoPlugin.h"

// ProToolkit drawing / annotation headers
#include <ProToolkit.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProDtlnote.h>
#include <ProDtlattach.h>
#include <ProDimension.h>
#include <ProArray.h>
#include <ProMdl.h>

// ProToolkit (and underlying Windows headers) may define min/max macros that
// interfere with std::min / std::max. Undefine them so <algorithm> works as
// expected.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

static bool PointInBorder(double x, double y,
    double minX, double minY,
    double maxX, double maxY)
{
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
}

static bool RectInBorder(double rMinX, double rMinY,
    double rMaxX, double rMaxY,
    double bMinX, double bMinY,
    double bMaxX, double bMaxY)
{
    return rMinX >= bMinX && rMaxX <= bMaxX
        && rMinY >= bMinY && rMaxY <= bMaxY;
}

// ---------------------------------------------------------------------------
// Drawing-view visitor  (visits only views on the target sheet)
// ---------------------------------------------------------------------------

struct ViewVisitData
{
    ProDrawing                  drawing;
    int                         targetSheet;
    double                      bMinX, bMinY, bMaxX, bMaxY;
    std::vector<ElementResult>* out;
    int                         index;
};

// Filter: accept only views that belong to targetSheet.
static ProError ViewFilterCb(ProDrawing drawing, ProView view, ProAppData data)
{
    auto* d = static_cast<ViewVisitData*>(data);
    int sheet = 0;
    if (ProDrawingViewSheetGet(drawing, view, &sheet) != PRO_TK_NO_ERROR)
        return PRO_TK_CONTINUE;
    return (sheet == d->targetSheet) ? PRO_TK_NO_ERROR : PRO_TK_CONTINUE;
}

// Action: compare the view outline against the border.
// ProDrawingViewOutlineGet returns outline[0]=upper-left, outline[1]=lower-right
// in drawing sheet coordinates.
static ProError ViewVisitCb(ProDrawing drawing, ProView view,
    ProError /*filterStatus*/, ProAppData data)
{
    auto* d = static_cast<ViewVisitData*>(data);

    ElementResult r;
    r.label = "DrawingView " + std::to_string(d->index++);
    r.isInside = false;

    ProPoint3d outline[2];
    if (ProDrawingViewOutlineGet(drawing, view, outline) == PRO_TK_NO_ERROR)
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

// ---------------------------------------------------------------------------
// Dimension visitor  (visits PRO_DIMENSION and PRO_REF_DIMENSION on target sheet)
// ---------------------------------------------------------------------------

struct DimVisitData
{
    ProDrawing                  drawing;
    int                         targetSheet;
    double                      bMinX, bMinY, bMaxX, bMaxY;
    std::vector<ElementResult>* out;
    int                         index;
};

// Filter: accept only dimensions whose display view is on targetSheet.
static ProError DimFilterCb(ProDimension* dim, ProAppData data)
{
    auto* d = static_cast<DimVisitData*>(data);

    ProView view;
    if (ProDrawingDimensionViewGet(d->drawing, dim, &view) != PRO_TK_NO_ERROR)
        return PRO_TK_CONTINUE;

    int sheet = 0;
    if (ProDrawingViewSheetGet(d->drawing, view, &sheet) != PRO_TK_NO_ERROR)
        return PRO_TK_CONTINUE;

    return (sheet == d->targetSheet) ? PRO_TK_NO_ERROR : PRO_TK_CONTINUE;
}

// Action: check text position, arrow endpoints, and witness-line endpoints.
static ProError DimVisitCb(ProDimension* dim, ProError /*filterStatus*/, ProAppData data)
{
    auto* d = static_cast<DimVisitData*>(data);

    ElementResult r;
    r.label = "Dimension " + std::to_string(d->index++);
    r.isInside = false;

    // Retrieve the view so we can ask ProDimensionLocationGet for drawing coords.
    ProView view;
    if (ProDrawingDimensionViewGet(d->drawing, dim, &view) != PRO_TK_NO_ERROR)
    {
        d->out->push_back(r);
        return PRO_TK_NO_ERROR;
    }

    ProDimlocation loc = nullptr;
    if (ProDimensionLocationGet(dim, view, d->drawing, &loc) != PRO_TK_NO_ERROR || !loc)
    {
        d->out->push_back(r);
        return PRO_TK_NO_ERROR;
    }

    r.isInside = true;

    // 1. Dimension text / value label position.
    {
        ProBoolean hasElbow;
        ProPoint3d textPt;
        double     elbowLen;
        if (ProDimlocationTextGet(loc, &hasElbow, textPt, &elbowLen) == PRO_TK_NO_ERROR)
        {
            if (!PointInBorder(textPt[0], textPt[1],
                d->bMinX, d->bMinY, d->bMaxX, d->bMaxY))
                r.isInside = false;
        }
    }

    // 2. Arrow-head positions.
    if (r.isInside)
    {
        ProPoint3d arr1, arr2;
        if (ProDimlocationArrowsGet(loc, arr1, arr2) == PRO_TK_NO_ERROR)
        {
            if (!PointInBorder(arr1[0], arr1[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY) ||
                !PointInBorder(arr2[0], arr2[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY))
                r.isInside = false;
        }
    }

    // 3. Witness-line endpoints (where dimension lines meet the geometry).
    if (r.isInside)
    {
        ProPoint3d wl1, wl2;
        if (ProDimlocationWitnesslinesGet(loc, wl1, wl2) == PRO_TK_NO_ERROR)
        {
            if (!PointInBorder(wl1[0], wl1[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY) ||
                !PointInBorder(wl2[0], wl2[1], d->bMinX, d->bMinY, d->bMaxX, d->bMaxY))
                r.isInside = false;
        }
    }

    ProDimlocationFree(loc);
    d->out->push_back(r);
    return PRO_TK_NO_ERROR;
}

// ---------------------------------------------------------------------------
// Note / balloon / leader-note check
// Covers ProDtlnote items collected by ProDrawingDtlnotesCollect, which
// includes regular notes, leader notes, and BOM balloons alike.
// ---------------------------------------------------------------------------

static bool NoteInBorder(ProDtlnote* note,
    double bMinX, double bMinY,
    double bMaxX, double bMaxY)
{
    // Check the 4-corner envelope of every text line.
    // ProDtlnoteLineEnvelopeGet returns envel[0..3] as:
    //   [0]=top-left  [1]=top-right  [2]=bottom-left  [3]=bottom-right
    // Iterate lines starting at 1 until the function signals no more lines.
    for (int line = 1; ; ++line)
    {
        ProVector env[4];
        if (ProDtlnoteLineEnvelopeGet(note, line, env) != PRO_TK_NO_ERROR)
            break;

        for (int c = 0; c < 4; ++c)
        {
            if (!PointInBorder(env[c][0], env[c][1], bMinX, bMinY, bMaxX, bMaxY))
                return false;
        }
    }

    // Check every leader attachment point.
    // PRODISPMODE_NUMERIC (0) retrieves evaluated note data.
    ProDtlnotedata notedata = nullptr;
    if (ProDtlnoteDataGet(note, nullptr, PRODISPMODE_NUMERIC, &notedata) == PRO_TK_NO_ERROR
        && notedata)
    {
        ProDtlattach* leaders = nullptr;
        if (ProDtlnotedataLeadersCollect(notedata, &leaders) == PRO_TK_NO_ERROR && leaders)
        {
            int count = 0;
            ProArraySizeGet((ProArray)leaders, &count);

            for (int i = 0; i < count; ++i)
            {
                ProDtlattachType atype;
                ProView          aview;
                ProVector        aloc;
                ProSelection     asel;

                if (ProDtlattachGet(leaders[i], &atype, &aview, aloc, &asel) == PRO_TK_NO_ERROR)
                {
                    if (!PointInBorder(aloc[0], aloc[1], bMinX, bMinY, bMaxX, bMaxY))
                    {
                        ProArrayFree((ProArray*)&leaders);
                        return false;
                    }
                }
            }
            ProArrayFree((ProArray*)&leaders);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

RuleCheckResult CreoPlugin::CheckSheetBorderRule()
{
    RuleCheckResult result;
    result.passed = false;

    // --- 1. Verify the active model is a drawing. ---
    ProMdl mdl = nullptr;
    if (ProMdlCurrentGet(&mdl) != PRO_TK_NO_ERROR || !mdl)
        return result;

    ProMdlType mdlType;
    if (ProMdlTypeGet(mdl, &mdlType) != PRO_TK_NO_ERROR || mdlType != PRO_MDL_DRAWING)
        return result;

    // ProDrawing is typedef struct drawing*; ProMdl is void* — safe C cast.
    ProDrawing drawing = (ProDrawing)mdl;

    // --- 2. Get the active (current) sheet number. ---
    int sheet = 0;
    if (ProDrawingCurrentSheetGet(drawing, &sheet) != PRO_TK_NO_ERROR)
        return result;

    // --- 3. Determine border extents.
    // Use ProDrawingFormatSizeGet (the format/border template dimensions) first.
    // Fall back to ProDrawingSheetSizeGet (physical sheet size) if no format is set.
    // The drawing coordinate origin is at the sheet lower-left corner (0, 0).
    ProPlotPaperSize paperSize;
    double           borderW = 0.0, borderH = 0.0;

    if (ProDrawingFormatSizeGet(drawing, sheet, &paperSize, &borderW, &borderH) != PRO_TK_NO_ERROR
        || borderW <= 0.0 || borderH <= 0.0)
    {
        if (ProDrawingSheetSizeGet(drawing, sheet, &paperSize, &borderW, &borderH) != PRO_TK_NO_ERROR
            || borderW <= 0.0 || borderH <= 0.0)
            return result;
    }

    const double bMinX = 0.0, bMinY = 0.0;
    const double bMaxX = borderW, bMaxY = borderH;

    // --- 4. Check all drawing views on this sheet. ---
    {
        ViewVisitData vd{ drawing, sheet, bMinX, bMinY, bMaxX, bMaxY, &result.elements, 1 };
        ProDrawingViewVisit(drawing, ViewVisitCb, ViewFilterCb, &vd);
    }

    // --- 5. Check all notes on this sheet.
    // ProDrawingDtlnotesCollect with a specific sheet number returns only notes
    // on that sheet; pass NULL for symbol to collect all note types (regular,
    // leader, and BOM balloon).
    {
        ProDtlnote* notes = nullptr;
        if (ProDrawingDtlnotesCollect(drawing, nullptr, sheet, &notes) == PRO_TK_NO_ERROR
            && notes)
        {
            int count = 0;
            ProArraySizeGet((ProArray)notes, &count);

            for (int i = 0; i < count; ++i)
            {
                ElementResult r;
                r.label = "Note " + std::to_string(i + 1);
                r.isInside = NoteInBorder(&notes[i], bMinX, bMinY, bMaxX, bMaxY);
                result.elements.push_back(r);
            }
            ProArrayFree((ProArray*)&notes);
        }
    }

    // --- 6. Check standard dimensions and reference dimensions on this sheet. ---
    {
        DimVisitData dd{ drawing, sheet, bMinX, bMinY, bMaxX, bMaxY, &result.elements, 1 };
        ProDrawingDimensionVisit(drawing, PRO_DIMENSION, DimVisitCb, DimFilterCb, &dd);
        ProDrawingDimensionVisit(drawing, PRO_REF_DIMENSION, DimVisitCb, DimFilterCb, &dd);
    }

    // --- 7. Rule passes only when every checked element is inside the border. ---
    result.passed = !result.elements.empty()
        && std::all_of(result.elements.begin(), result.elements.end(),
            [](const ElementResult& e) { return e.isInside; });

    return result;
}
