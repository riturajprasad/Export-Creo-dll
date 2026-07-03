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

// Optional request to show a user-facing popup, handled by the backend
// (FunctionExecutor.cpp calls into CommonUtility/PopUpFunctions.h). Leave
// `show` false (the default) if the rule needs no user interaction.
struct CREOPLUGIN_API PopUpRequest
{
    bool        show         = false;  // false: no popup; backend runs the rule as usual.
    std::string kind;                  // Required when show == true. One of:
                                        //   "YesNo"       -> PopUpFunctions::ShowYesNoPopUp
                                        //   "Informative" -> PopUpFunctions::ShowInformativePopUp
                                        //   "GetInteger"  -> PopUpFunctions::GetIntegerFromUser
    std::string message;                // Required when show == true.
    std::string title;                  // Optional; empty uses PopUpFunctions' own default title.
    bool        invertOutput = false;   // "YesNo" only: inverts the Yes/No -> true/false mapping.
    std::string defaultValue = "0";     // "GetInteger" only: pre-filled value in the input box.
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
    PopUpRequest               popUp;            // show == false by default (no popup).
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
