// CreoRuleContract.cpp
//
// Provides the two C-style exports that FunctionExecutor.cpp (in the Creo plugin
// backend) resolves at runtime via GetProcAddress:
//
//   CreoExecuteRule(const char* ruleJson, char** resultJson)  → int
//   CreoFreeResult (char* resultJson)                         → void
//
// Why a separate file?
//   CreoPlugin.cpp owns the ProToolkit geometry logic (RuleFunctions).
//   This file owns the DLL contract: JSON serialisation, memory management,
//   and the extern "C" boundary.  Keeping them separate makes each piece
//   easy to read and test in isolation.
//
// How the call flows end-to-end:
//   FunctionExecutor.cpp
//     └─ LoadLibraryExW("ExportCreodll.dll")
//         └─ GetProcAddress("CreoExecuteRule")   ← resolved here
//             └─ CreoExecuteRule(ruleJson, &resultJson)
//                 └─ CreoPlugin::RuleFunctions() ← ProToolkit geometry check
//                 └─ SerialiseResult()            ← C++ struct → JSON string
//                 └─ AllocCStr()                  ← heap-allocate for caller
//         └─ GetProcAddress("CreoFreeResult")     ← resolved here
//             └─ CreoFreeResult(resultJson)       ← free the heap buffer

#include "pch.h"
#include "CreoPlugin.h"

#include <cstdlib>   // malloc / free
#include <cstring>   // memcpy
#include <string>

// ── JSON escape ──────────────────────────────────────────────────────────────
// Escapes the characters that would break a JSON string value.
// Only these five need escaping for the entity-name strings produced by
// CreoPlugin::RuleFunctions().
static std::string JEsc(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:   o += static_cast<char>(c);
        }
    }
    return o;
}

// ── Serialise RuleCheckResult → JSON ─────────────────────────────────────────
// Produces the format that FunctionExecutor.cpp's ParseResultJson expects:
//
//   {"Status":bool,"Descriptions":[{"EntityName":"...","Status":bool}, ...]}
//
// where "Status" is the overall pass/fail flag and each Descriptions entry
// maps to one ElementResult (label → EntityName, isInside → Status).
static std::string SerialiseResult(const RuleCheckResult& res)
{
    std::string descs;
    for (const auto& el : res.elements) {
        if (!descs.empty()) descs += ",";
        const std::string name = el.label.empty() ? "Value not found" : el.label;
        descs += "{\"EntityName\":\"" + JEsc(name) + "\","
                  "\"Status\":"       + (el.isInside ? "true" : "false") + "}";
    }

    return std::string("{\"Status\":") + (res.passed ? "true" : "false") +
           ",\"Descriptions\":[" + descs + "]}";
}

// ── Heap-allocate a C string copy ────────────────────────────────────────────
// The caller (FunctionExecutor.cpp) frees this buffer via CreoFreeResult which
// calls free().  malloc/free must be used (not new/delete) because both sides
// may be compiled with different CRT versions; free() on the DLL side always
// matches the malloc() on the DLL side regardless of the host process CRT.
static char* AllocCStr(const std::string& s)
{
    char* buf = static_cast<char*>(malloc(s.size() + 1));
    if (buf)
        memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
}

// ── Exported C contract ──────────────────────────────────────────────────────
// extern "C" suppresses C++ name-mangling so GetProcAddress finds the symbols
// by their plain names "CreoExecuteRule" and "CreoFreeResult".

extern "C" {

// CreoExecuteRule
// ───────────────
// Parameters:
//   ruleJson   — rule-configuration JSON sent by the backend server.
//                Currently unused because RuleFunctions() reads geometry
//                directly from the active Creo drawing.  Reserved for future
//                rules that need server-supplied parameters (e.g. tolerances).
//   resultJson — OUT: on success, points to a malloc'd, null-terminated JSON
//                string.  The caller MUST free it with CreoFreeResult().
//                On failure the pointer is set to nullptr.
//
// Return value:
//    0  — success; *resultJson contains a valid JSON payload.
//   !0  — failure; *resultJson contains an error-description JSON payload
//         (still valid JSON so FunctionExecutor can surface the message).
//
// JSON payload on success:
//   {"Status":true,"Descriptions":[{"EntityName":"DrawingView 1","Status":true}, ...]}
//
// JSON payload on failure (exception path):
//   {"Status":false,"Descriptions":[{"EntityName":"<error text>","Status":false}]}
__declspec(dllexport) int CreoExecuteRule(const char* /*ruleJson*/, char** resultJson)
{
    if (!resultJson) return 1;
    *resultJson = nullptr;

    try {
        RuleCheckResult res = CreoPlugin::RuleFunctions();

        std::string json = SerialiseResult(res);
        *resultJson = AllocCStr(json);
        return (*resultJson) ? 0 : 1;   // non-null only when malloc succeeded
    }
    catch (...)
    {
        // Serialise the error as a valid JSON payload so FunctionExecutor.cpp
        // can display a readable message instead of a raw error code.
        const std::string err =
            "{\"Status\":false,\"Descriptions\":"
             "[{\"EntityName\":\"CreoExecuteRule threw an unexpected exception\","
               "\"Status\":false}]}";
        *resultJson = AllocCStr(err);
        return 1;
    }
}

// CreoFreeResult
// ──────────────
// Frees the buffer that CreoExecuteRule allocated via malloc().
// Must be called on the same DLL side that allocated the buffer so the correct
// CRT heap is used.  Passing nullptr is safe (free(nullptr) is a no-op).
__declspec(dllexport) void CreoFreeResult(char* resultJson)
{
    free(resultJson);
}

} // extern "C"
