#pragma once

#include <vector>
#include <string>

#ifdef EXPORTCREODLL_EXPORTS
#define CREOPLUGIN_API __declspec(dllexport)
#else
#define CREOPLUGIN_API __declspec(dllimport)
#endif

// C4251: std::string / std::vector members in exported structs — safe when both
// sides use the same CRT, which is guaranteed within the same Visual Studio solution.
#pragma warning(push)
#pragma warning(disable: 4251)

// Per-element check result: label describing the element and whether it is
// fully contained within the active sheet's format border.
struct CREOPLUGIN_API ElementResult
{
    std::string label;
    bool        isInside;
};

// Aggregate output for a rule check.
struct CREOPLUGIN_API RuleCheckResult
{
    std::vector<ElementResult> elements;         // one entry per drawing entity checked
    bool                       passed;           // final pass/fail, computed by this DLL
    bool                       matchAny = true;  // true: rule passes if ANY element passes (any_of);
                                                  // false: rule passes only if ALL elements pass (all_of).
                                                  // Sent to the backend as "MatchType" so it can be
                                                  // independently verified rather than trusted blindly.
};

#pragma warning(pop)

class CREOPLUGIN_API CreoPlugin
{
public:
    // Checks whether every drawing view, note (including balloons and leader
    // notes), and dimension on the active sheet is fully contained within the
    // sheet format border.  Must be called while a drawing is active in Creo.
    static RuleCheckResult RuleFunctions();
};
