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

    // Feature-visit context: accumulates one ElementResult per feature found
    // on the assembly's ProSolid.
    struct FeatureVisitData
    {
        std::vector<ElementResult>* out;
    };

    // Filter: accept every feature — the rule only cares that at least one
    // feature exists, so no feature is excluded from the visit.
    ProError FeatureFilterCb(ProFeature* feature, ProAppData appData)
    {
        return PRO_TK_NO_ERROR;
    }

    // Visit: there is no ProFeatureNameGet contract member — a feature's name
    // is retrieved via the generic ProModelitemNameGet, which explicitly
    // documents PRO_FEATURE as a valid item type. ProFeature's superobject is
    // ProModelitem (same underlying model-item layout), so the handle is
    // reinterpreted rather than converted through any extra API call.
    // Features with no name (lookup fails, e.g. PRO_TK_E_NOT_FOUND, or an
    // empty name comes back) are skipped — only named features are reported.
    ProError FeatureVisitCb(ProFeature* feature, ProError status, ProAppData appData)
    {
        auto* data = static_cast<FeatureVisitData*>(appData);
        if (!feature)
            return PRO_TK_NO_ERROR;

        ProName featureName{};
        if (g_api->ProModelitemNameGet(reinterpret_cast<ProModelitem*>(feature), featureName) != PRO_TK_NO_ERROR)
            return PRO_TK_NO_ERROR;

        std::string name = NarrowFromWide(featureName);
        if (name.empty())
            return PRO_TK_NO_ERROR;

        data->out->push_back({ name, true });
        return PRO_TK_NO_ERROR;
    }

}

// ── Public entry point ────────────────────────────────────────────────────────
//
// Rule: Assembly file must contain at least one Feature. Every feature found
// is reported by its real Creo name (ProModelitemNameGet) with isPass = true;
// aggregation is all_of, so the rule passes only when result.elements is
// non-empty and every reported feature passed (which they always do by
// construction) — i.e. passes iff >= 1 feature exists, fails iff none exist.

RuleCheckResult CreoPlugin::RuleFunctions()
{
    RuleCheckResult result;
    result.passed = false;
    result.matchAny = false;   // all_of: pass only if every reported element passes

    if (!g_api)
        return result;   // CreoInit was not called — should never happen in normal operation

    // Step 1: resolve the active Creo model.
    ProMdl mdl;
    if (g_api->ProMdlCurrentGet(&mdl) != PRO_TK_NO_ERROR)
    {
        result.elements.push_back({ "No active Creo model", false });
        return result;
    }

    // Step 2: confirm the active model is an assembly or part.
    ProMdlType mdlType;
    if (g_api->ProMdlTypeGet(mdl, &mdlType) != PRO_TK_NO_ERROR || (mdlType != PRO_MDL_ASSEMBLY && mdlType != PRO_MDL_PART))
    {
        result.elements.push_back({ "Active model is not an assembly or part", false });
        return result;
    }

    // Assemblies (like parts) are addressed through ProSolid.
    ProSolid solid = reinterpret_cast<ProSolid>(mdl);

    // Step 3: visit every feature owned by the assembly itself. ProSolidFeatVisit
    // is scoped to `solid` (this assembly's own ProSolid) — it does not recurse
    // into the separate ProMdl/ProSolid of each component part in the model
    // tree, so part-level features are never visited or reported here.
    // One ElementResult is collected per named assembly-level feature found.
    FeatureVisitData visitData{ &result.elements };
    g_api->ProSolidFeatVisit(solid, FeatureVisitCb, FeatureFilterCb, &visitData);

    // Step 4: no features found on the assembly — report failure explicitly.
    if (result.elements.empty())
        result.elements.push_back({ "No features found in assembly", false });

    // Step 5: aggregate — all_of over the collected elements.
    result.passed = std::all_of(result.elements.begin(), result.elements.end(),
        [](const ElementResult& e) { return e.isPass; });

    return result;
}
