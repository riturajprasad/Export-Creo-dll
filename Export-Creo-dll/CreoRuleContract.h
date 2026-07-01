#pragma once
// ============================================================================
// CreoRuleContract.h  —  Shared header for CreoRuleChecker and all rule DLLs
// ============================================================================
//
// PURPOSE
//   Rule DLLs are loaded via LoadLibrary (not through protk.dat), so their
//   copies of ProToolkit's static-library stubs (ucore.lib etc.) are NEVER
//   initialized by Creo.  Any direct ProToolkit call from a rule DLL crashes.
//
//   This header defines CreoApiContext, a table of ProToolkit function pointers
//   filled by the main plugin (CreoRuleChecker.dll, which IS properly
//   initialised) and handed to each rule DLL before execution.  Rule DLLs call
//   ProToolkit exclusively through this table — they never link protk_dll*.lib,
//   ucore.lib, or udata.lib.
//
// RULE DLL CONTRACT
//   A rule DLL MUST export (extern "C", __declspec(dllexport)):
//
//     void CreoInit(const CreoApiContext* api)
//       Called once per rule execution (before CreoExecuteRule).
//       Store `api` and use it for all ProToolkit calls.
//
//     int  CreoExecuteRule(const char* ruleJson, char** resultJson)
//       Execute the rule against the active Creo drawing.
//       Allocate *resultJson with malloc(); host frees it with CreoFreeResult().
//       Returns 0 on success, non-zero on failure.
//
//     void CreoFreeResult(char* resultJson)
//       Free the buffer returned by CreoExecuteRule (call free()).
//
// ADDING NEW APIs
//   Append new function pointers after the existing ones and bump `version`.
//   Older rule DLLs that were compiled against an earlier version of this
//   header will still work — they will simply ignore the new tail fields.
// ============================================================================

// ProToolkit type headers (type definitions only — no linking needed in rule DLLs)
#ifndef PRO_USE_VAR_ARGS
#define PRO_USE_VAR_ARGS
#endif
#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProDtlnote.h>
#include <ProDtlattach.h>
#include <ProDimension.h>
#include <ProArray.h>
#include <ProSelection.h>

// ── ProToolkit API table ─────────────────────────────────────────────────────
// All function signatures are taken verbatim from the PTC ProToolkit headers.
// Rule DLLs should access these via a stored pointer:
//
//   static const CreoApiContext* g_api = nullptr;
//   extern "C" void CreoInit(const CreoApiContext* api) { g_api = api; }
//
//   // … then inside rule logic:
//   ProMdl mdl;
//   g_api->ProMdlCurrentGet(&mdl);

struct CreoApiContext
{
    int version; // = 1

    // ── Model / drawing identity ─────────────────────────────────────────────
    ProError (*ProMdlCurrentGet)(ProMdl* p_handle);
    ProError (*ProMdlTypeGet)   (ProMdl handle, ProMdlType* p_type);

    // ── Sheet / format border ────────────────────────────────────────────────
    ProError (*ProDrawingCurrentSheetGet)(ProDrawing drawing, int* p_sheet);
    ProError (*ProDrawingFormatSizeGet)  (ProDrawing drawing, int sheet,
                                          ProPlotPaperSize* p_size,
                                          double* p_width, double* p_height);
    ProError (*ProDrawingSheetSizeGet)   (ProDrawing drawing, int sheet,
                                          ProPlotPaperSize* p_size,
                                          double* p_width, double* p_height);

    // ── Drawing views ────────────────────────────────────────────────────────
    ProError (*ProDrawingViewVisit)     (ProDrawing drawing,
                                         ProViewVisitAction  visit_action,
                                         ProViewFilterAction filter_action,
                                         ProAppData app_data);
    ProError (*ProDrawingViewSheetGet)  (ProDrawing drawing, ProView view,
                                         int* p_sheet);
    ProError (*ProDrawingViewOutlineGet)(ProDrawing drawing, ProView view,
                                         ProPoint3d outline[2]);

    // ── Detail notes ─────────────────────────────────────────────────────────
    ProError (*ProDrawingDtlnotesCollect)    (ProDrawing drawing,
                                              ProDtlsymdef* symbol, int sheet,
                                              ProDtlnote** p_notes);
    ProError (*ProDtlnoteLineEnvelopeGet)    (ProDtlnote* note, int line,
                                              ProVector env[4]);
    ProError (*ProDtlnoteDataGet)            (ProDtlnote* note, ProDtlsymdef* symbol,
                                              ProDisplayMode mode,
                                              ProDtlnotedata* p_data);
    ProError (*ProDtlnotedataLeadersCollect) (ProDtlnotedata data,
                                              ProDtlattach** p_leaders);
    ProError (*ProDtlattachGet)              (ProDtlattach attach,
                                              ProDtlattachType* p_type,
                                              ProView* p_view,
                                              ProVector p_loc,
                                              ProSelection* p_sel);

    // ── Dimensions ───────────────────────────────────────────────────────────
    ProError (*ProDrawingDimensionVisit)    (ProDrawing drawing,
                                             ProType type,
                                             ProDimensionVisitAction  visit_action,
                                             ProDimensionFilterAction filter_action,
                                             ProAppData app_data);
    ProError (*ProDrawingDimensionViewGet)  (ProDrawing drawing,
                                             ProDimension* p_dim,
                                             ProView* p_view);
    ProError (*ProDimensionLocationGet)     (ProDimension* p_dim, ProView view,
                                             ProDrawing drawing,
                                             ProDimlocation* p_location);
    ProError (*ProDimlocationTextGet)       (ProDimlocation location,
                                             ProBoolean* p_has_elbow,
                                             ProPoint3d  p_text,
                                             double*     p_elbow_length);
    ProError (*ProDimlocationArrowsGet)     (ProDimlocation location,
                                             ProPoint3d p_arr1,
                                             ProPoint3d p_arr2);
    ProError (*ProDimlocationWitnesslinesGet)(ProDimlocation location,
                                              ProPoint3d p_wl1,
                                              ProPoint3d p_wl2);
    ProError (*ProDimlocationFree)          (ProDimlocation location);

    // ── Array utilities ──────────────────────────────────────────────────────
    ProError (*ProArraySizeGet)(ProArray  arr,  int* p_size);
    ProError (*ProArrayFree)   (ProArray* p_arr);
};
