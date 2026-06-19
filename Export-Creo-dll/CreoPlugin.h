#pragma once

#include <vector>
#include <string>

// Per-element check result: label describing the element and whether it is
// fully contained within the active sheet's format border.
struct ElementResult
{
    std::string label;
    bool        isInside;
};

// Aggregate output for the "Nothing outside sheet border" rule.
struct RuleCheckResult
{
    std::vector<ElementResult> elements; // one entry per drawing entity checked
    bool                       passed;   // true only if every element is inside
};

class CreoPlugin
{
public:
    // Checks whether every drawing view, note (including balloons and leader
    // notes), and dimension on the active sheet is fully contained within the
    // sheet format border.  Must be called while a drawing is active in Creo.
    static RuleCheckResult CheckSheetBorderRule();
};
