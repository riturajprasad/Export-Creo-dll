#include "pch.h"
#include "CreoPlugin.h"

// CreoRuleContract.h  ← shared with CreoRuleChecker (the host plugin).
// Defines CreoApiContext: the table of ProToolkit function pointers that the
// host passes via CreoInit() before calling CreoExecuteRule().
// This DLL does NOT link protk_dll*.lib / ucore.lib / udata.lib — all
// ProToolkit calls are made through the function pointers below.
#include "CreoRuleContract.h"

#include <algorithm>
#include <cmath>
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
    // Standard scale ratios (drawing-unit : model-unit). Any view whose scale
    // matches one of these (within floating-point tolerance) passes; every
    // other regular view fails.
    const double kStandardScales[] = {
        1.0,        // 1:1
        0.5,        // 1:2
        0.2,        // 1:5
        0.1,        // 1:10
        0.05,       // 1:20
        0.02,       // 1:50
        0.01,       // 1:100
        2.0,        // 2:1
        5.0,        // 5:1
        10.0,       // 10:1
    };

    bool ScaleEquals(double a, double b)
    {
        return std::fabs(a - b) < 1e-4;
    }

    bool IsStandardScale(double scale)
    {
        for (double standard : kStandardScales)
            if (ScaleEquals(scale, standard))
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

    // Creo has no dedicated "isometric view" type — an isometric view is a
    // general view placed with the isometric default orientation, and its
    // name is what the user (or the "iso"/"isometric" default view name)
    // gave it. Matched the same way other rules in this project match
    // free-text phrases: case-insensitive substring search.
    bool IsIsometricViewName(const std::wstring& name)
    {
        const std::wstring needle = L"ISO";
        auto it = std::search(name.begin(), name.end(), needle.begin(), needle.end(),
            [](wchar_t a, wchar_t b) { return std::towupper(a) == std::towupper(b); });
        return it != name.end();
    }

    // One drawing view on the active sheet, with everything needed to decide
    // whether its scale satisfies the rule.
    struct ViewInfo
    {
        std::string label;
        int          id          = 0;
        double       scale       = 0.0;
        bool         hasParent   = false; // true for views projected/derived from another view
        bool         isExempt    = false; // isometric or exploded — any scale is valid
    };

    // Collects every view on `sheet` that actually represents drawing
    // geometry — erased, background/overlay (format) and draft (sketch)
    // views carry no meaningful "view scale" for this rule and are skipped.
    std::vector<ViewInfo> CollectSheetViews(ProDrawing drawing, int sheet)
    {
        std::vector<ViewInfo> out;

        ProView* views = nullptr;
        if (g_api->ProDrawingViewsCollect(drawing, &views) != PRO_TK_NO_ERROR || !views)
            return out;

        int count = 0;
        g_api->ProArraySizeGet(reinterpret_cast<ProArray>(views), &count);

        for (int i = 0; i < count; ++i)
        {
            ProView view = views[i];

            int viewSheet = 0;
            if (g_api->ProDrawingViewSheetGet(drawing, view, &viewSheet) != PRO_TK_NO_ERROR
                || viewSheet != sheet)
                continue;

            ProBoolean flag = PRO_B_FALSE;
            if (g_api->ProDrawingViewIsErased(drawing, view, &flag) == PRO_TK_NO_ERROR && flag)
                continue;
            if (g_api->ProDrawingViewIsBackground(drawing, view, &flag) == PRO_TK_NO_ERROR && flag)
                continue;
            if (g_api->ProDrawingViewIsOverlay(drawing, view, &flag) == PRO_TK_NO_ERROR && flag)
                continue;
            if (g_api->ProDrawingViewIsDraft(drawing, view, &flag) == PRO_TK_NO_ERROR && flag)
                continue;

            ViewInfo info;

            g_api->ProDrawingViewIdGet(drawing, view, &info.id);

            double scale = 0.0;
            if (g_api->ProDrawingViewScaleGet(drawing, view, &scale) != PRO_TK_NO_ERROR)
                continue;   // scale not meaningful for this view — nothing to check
            info.scale = scale;

            ProView parent = nullptr;
            info.hasParent = g_api->ProDrawingViewParentGet(drawing, view, &parent) == PRO_TK_NO_ERROR
                && parent != nullptr;

            ProBoolean exploded = PRO_B_FALSE;
            const bool isExploded = g_api->ProDrawingViewExplodedGet(drawing, view, &exploded) == PRO_TK_NO_ERROR
                && exploded;

            ProName wname{};
            std::wstring name;
            if (g_api->ProDrawingViewNameGet(drawing, view, wname) == PRO_TK_NO_ERROR)
                name = wname;

            info.isExempt = isExploded || IsIsometricViewName(name);
            info.label = name.empty() ? ("View " + std::to_string(info.id)) : NarrowFromWide(name);

            out.push_back(info);
        }

        g_api->ProArrayFree(reinterpret_cast<ProArray*>(&views));
        return out;
    }

}

// ── Public entry point ────────────────────────────────────────────────────────
//
// Rule: every view on the active sheet must use one of the standard scales
// (1:1, 1:2, 1:5, 1:10, 1:20, 1:50, 1:100, 2:1, 5:1, 10:1) — except Isometric
// and Exploded views, for which any scale is valid. In addition, the first
// (lowest-id, non-derived) view's scale must be the one loaded on the title
// block, i.e. it must match the sheet's title-block scale parameter.
// All views must pass (all_of), matching the "all_of View Name" contract.

RuleCheckResult CreoPlugin::RuleFunctions()
{
    RuleCheckResult result;
    result.passed  = false;
    result.matchAny = false;   // all_of: every View Name must pass

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

    std::vector<ViewInfo> views = CollectSheetViews(drawing, activeSheet);
    if (views.empty())
    {
        result.elements.push_back({ "No views found on active sheet", false });
        return result;
    }

    // The first view is the lowest-id view that isn't derived from another
    // view (projection/detail/section views inherit their placement from a
    // parent and are never "the first view" placed on the sheet).
    const ViewInfo* firstView = nullptr;
    for (const ViewInfo& v : views)
        if (!v.hasParent && (!firstView || v.id < firstView->id))
            firstView = &v;

    // Title-block scale: the &pdm_scale-style value Creo displays in the
    // title block for this sheet. Retrieved for the drawing's primary solid;
    // if unavailable, the title-block condition is skipped for lack of data
    // rather than forced to fail.
    double titleBlockScale = 0.0;
    bool haveTitleBlockScale = false;
    {
        ProSolid solid = nullptr;
        g_api->ProDrawingCurrentsolidGet(drawing, &solid);
        haveTitleBlockScale =
            g_api->ProDrawingScaleGet(drawing, solid, activeSheet, &titleBlockScale) == PRO_TK_NO_ERROR;
    }

    for (const ViewInfo& v : views)
    {
        std::vector<std::string> reasons;

        const bool scaleOk = v.isExempt || IsStandardScale(v.scale);
        if (!scaleOk)
            reasons.push_back("Wrong Scale");

        if (&v == firstView && haveTitleBlockScale && !ScaleEquals(v.scale, titleBlockScale))
            reasons.push_back("Scale Not Loaded on Title Block");

        const bool pass  = reasons.empty();
        std::string label = v.label;
        if (!pass)
        {
            label += " (";
            for (size_t i = 0; i < reasons.size(); ++i)
            {
                if (i > 0) label += ", ";
                label += reasons[i];
            }
            label += ")";
        }

        result.elements.push_back({ label, pass });
    }

    result.passed = std::all_of(result.elements.begin(), result.elements.end(),
        [](const ElementResult& e) { return e.isPass; });

    return result;
}
