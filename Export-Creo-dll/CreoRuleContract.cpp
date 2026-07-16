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

// ── Serialise PopUpRequest → JSON ────────────────────────────────────────────
static std::string SerialisePopUp(const PopUpRequest& p)
{
    return std::string("{\"Show\":")   + (p.show ? "true" : "false") + "," +
           "\"Kind\":\""               + JEsc(p.kind)         + "\"," +
           "\"Message\":\""            + JEsc(p.message)      + "\"," +
           "\"Title\":\""              + JEsc(p.title)        + "\"," +
           "\"InvertOutput\":"         + (p.invertOutput ? "true" : "false") + "," +
           "\"DefaultValue\":\""       + JEsc(p.defaultValue) + "\"}";
}

// ── Serialise RuleCheckResult → JSON ─────────────────────────────────────────
// Produces the format that FunctionExecutor.cpp's ParseResultJson expects:
//
//   {"Status":bool,"MatchType":"Any"|"All","PopUp":{...},
//    "Descriptions":[{"EntityName":"...","Status":bool}, ...]}
//
// "Status" is this DLL's own pass/fail computation (kept for backward
// compatibility with older backends). "MatchType" tells the backend which
// combinator this rule intends — "Any" if the rule should pass as soon as one
// EntityName passes, "All" if every EntityName must pass — so the backend can
// independently re-derive the final RuleStatus from Descriptions rather than
// trusting this DLL's Status bit blindly. "PopUp" tells the backend whether
// this rule needs a user-facing popup and, if so, which PopUpFunctions.h
// function to call and with what parameters — see PopUpRequest in CreoPlugin.h.
// Each Descriptions entry maps to one ElementResult (label → EntityName,
// isInside → Status).
static std::string SerialiseResult(const RuleCheckResult& res)
{
    std::string descs;
    for (const auto& el : res.elements) {
        if (!descs.empty()) descs += ",";
        const std::string name = el.entityName.empty() ? "Value not found" : el.entityName;
        descs += "{\"EntityName\":\"" + JEsc(name) + "\","
                  "\"Status\":"       + (el.isPass ? "true" : "false") + "}";
    }

    return std::string("{\"Status\":") + (res.passed ? "true" : "false") +
           ",\"MatchType\":\"" + (res.matchAny ? "Any" : "All") + "\"" +
           ",\"PopUp\":" + SerialisePopUp(res.popUp) +
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
//   {"Status":true,"MatchType":"Any","Descriptions":[{"EntityName":"DrawingView 1","Status":true}, ...]}
//
// JSON payload on failure (exception path):
//   {"Status":false,"MatchType":"Any","Descriptions":[{"EntityName":"<error text>","Status":false}]}
__declspec(dllexport) int CreoExecuteRule(const char* /*ruleJson*/, char** resultJson)
{
    if (!resultJson) return 1;
    *resultJson = nullptr;

    // CreoPlugin::RuleFunctions() calls ProToolkit APIs (ProMdlCurrentGet,
    // ProDrawingViewVisit, ProDrawingDtlnotesCollect, etc.).  Those APIs are
    // only safe on Creo's main thread.  The Creo plugin backend ensures this
    // function is invoked from the main thread via MainThreadDispatcher —
    // a hidden HWND_MESSAGE window that receives PostMessage from the WebSocket
    // worker thread and drains the work queue in its WndProc.
    try
    {
        RuleCheckResult res = CreoPlugin::RuleFunctions();
        std::string json    = SerialiseResult(res);
        *resultJson = AllocCStr(json);
        return (*resultJson) ? 0 : 1;
    }
    catch (...)
    {
        const std::string err =
            "{\"Status\":false,\"Descriptions\":["
             "{\"EntityName\":\"CreoExecuteRule threw an unexpected C++ exception\","
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
