#pragma once
// ============================================================================
// CreoRuleContract.h  —  Shared API bridge for CreoRuleChecker and all rule DLLs
// Version: 4
// ============================================================================
//
// PURPOSE
//   ProToolkit static libs (ucore.lib, udata.lib, protk_dll*.lib) compile
//   per-DLL session state.  A DLL loaded via LoadLibrary never receives
//   user_initialize_plus, so its ProToolkit state is uninitialised — any
//   direct ProToolkit call crashes.
//
//   This header defines CreoApiContext: a table of ProToolkit function
//   pointers filled by the main plugin (CreoRuleChecker.dll, which IS
//   properly initialised) and handed to each rule DLL before execution.
//   Rule DLLs call ProToolkit exclusively through this table — they never
//   link protk_dll*.lib, ucore.lib, or udata.lib.
//
// RULE DLL CONTRACT
//   A rule DLL MUST export (extern "C" __declspec(dllexport)):
//
//     void CreoInit(const CreoApiContext* api)
//       Called before every CreoExecuteRule.  Store api and use it for
//       all ProToolkit calls:   g_api->ProMdlCurrentGet(&mdl);
//
//     int  CreoExecuteRule(const char* ruleJson, char** resultJson)
//       Execute the rule.  Allocate *resultJson with malloc(); the host
//       frees it via CreoFreeResult().  Return 0 on success, non-zero on fail.
//
//     void CreoFreeResult(char* resultJson)
//       Call free() on the buffer returned by CreoExecuteRule.
//
// EXTENDING THE CONTRACT
//   Append new function pointers after the last field and bump `version`.
//   Older rule DLLs compiled against an earlier version still work — they
//   simply ignore the new tail entries.
//   When adding entries: ALSO add the corresponding fill in MainThreadDispatcher.cpp
//   (one extra line in the s_api initialiser) and rebuild CreoRuleChecker.dll.
//
// COVERAGE (version 4)
//   Drawing/Sheet · Views · Dimensions · Detail Notes · Attachments
//   GD&T · Annotations · Drawing Tables · 3D Tables · Draft Entities
//   Detail Groups · Symbol Instances · Layers · Model/Solid · Selection · Array
//   Text Style · Symbol Definitions · Solid/Feature · Simplified Reps
//   (Removed: 3D Model Notes · Set Datum Tags · Surface Finish · Annotation Elements
//    — all require TOOLKIT-for-3D_Drawings license)
// ============================================================================

#ifndef PRO_USE_VAR_ARGS
#define PRO_USE_VAR_ARGS
#endif

// ProToolkit type headers  (types only — this DLL does NOT link protk libs)
#include <ProToolkit.h>
#include <ProObjects.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProView.h>
#include <ProDtlitem.h>
#include <ProDtlnote.h>
#include <ProDtlattach.h>
#include <ProDtlentity.h>
#include <ProDtlgroup.h>
#include <ProDtlsymdef.h>
#include <ProDtlsyminst.h>
#include <ProDimension.h>
#include <ProNote.h>
#include <ProAnnotation.h>
#include <ProAnnotationElem.h>
#include <ProGtol.h>
#include <ProGtolAttach.h>
#include <ProBomballoon.h>
#include <ProDwgtable.h>
#include <ProTable.h>
#include <ProLayer.h>
#include <ProLayerstate.h>
#include <ProSolid.h>
#include <ProFeature.h>
#include <ProSurface.h>
#include <ProArray.h>
#include <ProSelection.h>
#include <ProMdlUnits.h>
#include <ProSimprep.h>
#include <ProSimprepdata.h>
#include <ProXsec.h>

// ── ProToolkit API table ─────────────────────────────────────────────────────
// Rule DLLs store the pointer:
//
//   static const CreoApiContext* g_api = nullptr;
//   extern "C" void CreoInit(const CreoApiContext* api) { g_api = api; }
//
// Then call:
//   ProMdl mdl;  g_api->ProMdlCurrentGet(&mdl);

struct CreoApiContext
{
    int version; // = 4

    // ════════════════════════════════════════════════════════════════════════
    // §1  MODEL / DRAWING IDENTITY
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProMdlCurrentGet)      (ProMdl* p_handle);
    ProError(*ProMdlTypeGet)         (ProMdl handle, ProMdlType* p_type);
    ProError(*ProMdlNameGet)         (ProMdl handle, ProName name);
    ProError(*ProMdlMdlnameGet)      (ProMdl handle, ProMdlName name);
    ProError(*ProMdlIdGet)           (ProMdl model, int* p_id);
    ProError(*ProMdlSubtypeGet)      (ProMdl model, ProMdlsubtype* subtype);
    ProError(*ProMdlFiletypeGet)     (ProMdl model, ProMdlfileType* filetype);
    ProError(*ProMdlModificationVerify)(ProMdl handle, ProBoolean* p_modified);
    ProError(*ProMdlToModelitem)     (ProMdl mdl, ProModelitem* p_model_item);
    ProError(*ProMdlDetailOptionGet) (ProMdl mdl, ProName option, ProLine value);
    ProError(*ProMdlOriginGet)       (ProMdl handle, ProPath origin);
    ProError(*ProMdlWindowGet)       (ProMdl mdl, int* window_id);
    ProError(*ProSessionMdlList)     (ProMdlType model_type, ProMdl** p_model_array, int* p_count);
    ProError(*ProMdlGtolVisit)       (ProMdl model,
        ProGtolVisitAction visit_action,
        ProGtolFilterAction filter_action,
        ProAppData data);
    ProError(*ProMdlAnnotationplanesCollect)(ProMdl mdl, wchar_t*** names);

    // ════════════════════════════════════════════════════════════════════════
    // §2  DRAWING SHEETS
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingCurrentSheetGet)     (ProDrawing drawing, int* current_sheet);
    ProError(*ProDrawingSheetsCount)         (ProDrawing drawing, int* n_sheets);
    ProError(*ProDrawingSheetNameGet)        (ProDrawing drawing, int sheet, ProName sheet_name);
    ProError(*ProDrawingSheetInfoGet)        (ProDrawing drawing, int sheet, ProDrawingSheetInfo* sheet_info);
    ProError(*ProDrawingSheetSizeGet)        (ProDrawing drawing, int sheet,
        ProPlotPaperSize* type, double* width, double* height);
    ProError(*ProDrawingSheetOrientationGet) (ProDrawing drawing, int sheet, ProSheetOrientation* orientation);
    ProError(*ProDrawingSheetUnitsGet)       (ProDrawing drawing, int sheet, ProUnititem* units);
    ProError(*ProDrawingSheetTrfGet)         (ProDrawing drawing, int sheet,
        ProName sheet_size, ProMatrix transform);
    ProError(*ProDrawingSheetFromFormatGet)  (ProDrawing drawing, int drawing_sheet, int* format_sheet);
    ProError(*ProDrawingSheetFormatIsBlanked)(ProDrawing drawing, int sheet, ProBoolean* is_blanked);
    ProError(*ProDrawingSheetFormatIsShown)  (ProDrawing drawing, int sheet, ProBoolean* is_shown);
    ProError(*ProDrawingScaleGet)            (ProDrawing drawing, ProSolid solid, int sheet, double* scale);
    ProError(*ProDrawingFormatGet)           (ProDrawing drawing, int sheet, ProName format_name);
    ProError(*ProDrawingFormatSizeGet)       (ProDrawing drawing, int drawing_sheet,
        ProPlotPaperSize* p_size, double* p_width, double* p_height);
    ProError(*ProDrawingToleranceStandardGet)(ProDrawing p_draw, ProStandard* p_standard);
    ProError(*ProDrawingDualDimensionGet)    (ProDrawing drawing,
        ProDualDimensionType* dual_dimensioning,
        ProName dual_secondary_units,
        int* dual_digits_diff,
        ProBoolean* dual_dimension_brackets);
    ProError(*ProDrawingPosToLocgrid)        (ProDrawing p_draw, int sheet,
        ProVector pos, ProName column_label, ProName row_label);
    ProError(*ProDrawingSetupOptionGet)      (ProDrawing drawing, ProName option, ProLine value);

    // ════════════════════════════════════════════════════════════════════════
    // §3  DRAWING VIEWS — iteration & basic properties
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingViewVisit)      (ProDrawing drawing,
        ProViewVisitAction   visit_action,
        ProViewFilterAction  filter_action,
        ProAppData app_data);
    ProError(*ProDrawingViewsCollect)   (ProDrawing drawing, ProView** views);
    ProError(*ProDrawingViewSheetGet)   (ProDrawing drawing, ProView view, int* sheet);
    ProError(*ProDrawingErasedviewSheetGet)(ProDrawing drawing, ProView erased_view, int* sheet);
    ProError(*ProDrawingViewOutlineGet) (ProDrawing drawing, ProView view, ProPoint3d outline[2]);
    ProError(*ProDrawingViewScaleGet)   (ProDrawing drawing, ProView view, double* scale);
    ProError(*ProDrawingViewScaleIsUserdefined)(ProDrawing drawing, ProView view, ProBoolean* p_is_userdefined);
    ProError(*ProDrawingViewTransformGet)(ProDrawing drawing, ProView view,
        ProBoolean view_to_drawing, ProMatrix transform);
    ProError(*ProDrawingViewIsOverlay)  (ProDrawing drawing, ProView view, ProBoolean* is_overlay);
    ProError(*ProDrawingOverlayviewGet) (ProDrawing drawing, int sheet, ProView* overlay_view);
    ProError(*ProDrawingViewIsBackground)(ProDrawing drawing, ProView view, ProBoolean* is_background);
    ProError(*ProDrawingBackgroundViewGet)(ProDrawing drawing, int sheet, ProView* background_view);
    ProError(*ProDrawingViewNameGet)    (ProDrawing drawing, ProView view, ProName name);
    ProError(*ProDrawingViewIdGet)      (ProDrawing drawing, ProView view, int* view_id);
    ProError(*ProDrawingViewInit)       (ProDrawing drawing, int view_id, ProView* view);
    ProError(*ProDrawingViewSolidGet)   (ProDrawing drawing, ProView view, ProSolid* solid);
    ProError(*ProDrawingViewNeedsRegen) (ProDrawing drawing, ProView view, ProBoolean* needs_regen);
    ProError(*ProDrawingViewDisplayGet) (ProDrawing drawing, ProView view, ProDrawingViewDisplay* display_status);
    ProError(*ProDrawingViewPipingdisplayGet)(ProDrawing drawing, ProView view, ProPipingDisplay* piping_display);
    ProError(*ProDrawingViewZclippingGet)(ProDrawing drawing, ProView view, ProSelection* zclip_geometry);
    ProError(*ProDrawingViewDatumdisplayGet)(ProDrawing drawing, ProView view,
        ProSelection datum, ProViewItemdisplayStatus* status);
    ProError(*ProDrawingDraftViewsCollect)(ProDrawing drawing, ProView** views);
    ProError(*ProDrawingViewIsDraft)    (ProDrawing drawing, ProView view, ProBoolean* is_draft);

    // ════════════════════════════════════════════════════════════════════════
    // §4  VIEW PROPERTIES  (ProDrawingView.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingViewTypeGet)    (ProDrawing drawing, ProView view, ProViewType* type);
    ProError(*ProDrawingViewFlagGet)    (ProDrawing drawing, ProView view, ProBoolean* flag);
    ProError(*ProDrawingViewParentGet)  (ProDrawing drawing, ProView view, ProView* parent_view);
    ProError(*ProDrawingViewSegmentedLeaderGet)(ProDrawing drawing, ProView view, ProView* leader_view);
    ProError(*ProDrawingViewChildrenGet)(ProDrawing drawing, ProView view,
        ProView** view_children, int* no_children);
    ProError(*ProDrawingViewSimplifiedGet)(ProDrawing drawing, ProView view, ProSimprep* simplified_ref);
    ProError(*ProDrawingViewDetailReferenceGet)(ProDrawing drawing, ProView view, ProSelection* ref_sel);
    ProError(*ProDrawingViewDetailCurvedataGet)(ProDrawing drawing, ProView view, ProCurvedata* curve_data);
    ProError(*ProDrawingViewDetailBoundaryGet) (ProDrawing drawing, ProView view,
        ProViewDetailBoundaryType* type, ProBoolean* show);
    ProError(*ProDrawingViewVisibleareaTypeGet)(ProDrawing drawing, ProView view,
        ProDrawingViewVisibleareaType* visible_area);
    ProError(*ProDrawingViewPartialVisibleAreaGet)(ProDrawing drawing, ProView parent_view,
        ProSelection* ref_point, ProCurvedata* curve_data,
        ProBoolean* show_boundary);
    ProError(*ProDrawingViewBrokenNumberGet)   (ProDrawing drawing, ProView broken_view, int* number);
    ProError(*ProDrawingViewBrokenVisibleAreaGet)(ProDrawing drawing, ProView parent_view, int index,
        ProViewBrokenDir* dir,
        ProSelection* first_sel, ProSelection* second_sel,
        ProViewBrokenLineStyle* line_style, ProCurvedata* curve_data);
    ProError(*ProDrawingViewSectionTypeGet)    (ProDrawing drawing, ProView view,
        ProDrawingViewSectionType* section_type);
    ProError(*ProDrawingView2DSectionNumberGet)(ProDrawing drawing, ProView view, int* number);
    ProError(*ProDrawingView2DSectionGet)      (ProDrawing drawing, ProView view, int index,
        ProName sec_name,
        ProDrawingViewSectionAreaType* sec_area_type,
        ProSelection* ref_sel, ProCurvedata* curve_data,
        ProView* arrow_display_view);
    ProError(*ProDrawingView3DSectionGet)      (ProDrawing drawing, ProView view,
        ProName sec_name, ProBoolean* show_x_hatch);
    ProError(*ProDrawingViewSinglepartSectionGet)(ProDrawing drawing, ProView view, ProSelection* ref_sel);
    ProError(*ProDrawingViewExplodedGet)       (ProDrawing drawing, ProView view, ProBoolean* exploded_state);
    ProError(*ProDrawingViewOriginGet)         (ProDrawing drawing, ProView view,
        ProPoint3d location, ProSelection* sel_ref);
    ProError(*ProDrawingViewAlignmentGet)      (ProDrawing drawing, ProView view,
        ProDrawingViewAlignStyle* align_style,
        ProView* view_reference,
        ProSelection* align_ref_1, ProSelection* align_ref_2);
    ProError(*ProDrawingViewColorSourceGet)    (ProDrawing drawing, ProView view,
        ProDrawingViewColorSource* color_source);
    ProError(*ProDrawingViewPerspectiveScaleGet)(ProDrawing drawing, ProView view,
        double* eye_dist, double* view_dia);
    ProError(*ProDrawingViewIsErased)          (ProDrawing drawing, ProView view, ProBoolean* is_erased);
    ProError(*ProDrawingViewRevolveInfoGet)    (ProDrawing drawing, ProView view,
        ProXsec* x_sec, ProSelection* selection,
        ProPoint3d view_location);
    ProError(*ProDrawingView2DSectionFlipGet)  (ProDrawing drawing, ProView view,
        int index, ProBool* p_flip);
    ProError(*ProDrawingViewHalfVisibleAreaGet)(ProDrawing drawing, ProView view,
        ProSelection* plane_ref, ProBoolean* keep_side,
        ProDrawingLineStandardType* line_standard);
    ProError(*ProDrawingViewAuxiliaryInfoGet)  (ProDrawing drawing, ProView view,
        ProSelection* selection, ProPoint3d view_location);

    // ════════════════════════════════════════════════════════════════════════
    // §5  DRAWING SOLIDS / SIMPREPS
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingSolidsVisit)   (ProDrawing drawing,
        ProSolidVisitAction   visit_action,
        ProSolidFilterAction  filter_action,
        ProAppData app_data);
    ProError(*ProDrawingSolidsCollect) (ProDrawing drawing, ProSolid** solids);
    ProError(*ProDrawingCurrentsolidGet)(ProDrawing drawing, ProSolid* solid);
    ProError(*ProDrawingSimprepsCollect)(ProDrawing drawing, ProSolid solid, ProSimprep** simpreps);

    // ════════════════════════════════════════════════════════════════════════
    // §6  DIMENSIONS
    // ════════════════════════════════════════════════════════════════════════
    // — drawing-scope visitors —
    ProError(*ProDrawingDimensionVisit)  (ProDrawing drawing, ProType type,
        ProDimensionVisitAction  action,
        ProDimensionFilterAction filter,
        ProAppData data);
    ProError(*ProDrawingDimensionViewGet)(ProDrawing drawing, ProDimension* dimension, ProView* view);
    ProError(*ProDrawingDimensionPosGet) (ProDrawing drawing, ProDimension* dimension, ProVector location);
    ProError(*ProDrawingDimAttachsGet)   (ProDrawing drawing, ProDimension* dimension,
        ProSelection** p_attachments_arr, ProDimSense** p_dsense_arr);
    ProError(*ProDrawingDimAttachpointsGet)(ProDrawing drawing, ProDimension* dimension,
        ProDimAttachment** attachments_arr, ProDimSense** dsense_arr);
    ProError(*ProDrawingDimAttachpointsViewGet)(ProDrawing drawing, ProDimension* dimension,
        ProDimAttachment** attachments_arr, ProDimSense** dsense_arr);
    ProError(*ProDrawingDimIsAssociative)(ProDrawing drawing, ProDimension* dimension, ProBoolean* associative);
    ProError(*ProDrawingDimIsOrdinate)   (ProDrawing drawing, ProDimension* dimension,
        ProBoolean* ordinate, ProDimension* base_dim);
    ProError(*ProDrawingDimensionIsDisplayed)(ProDrawing drawing, ProDimension* dimension, ProBoolean* displayed);
    ProError(*ProDrawingDimensionIsToleranceDisplayed)(ProDrawing p_draw,
        ProDimension* dimension, ProBoolean* tolerance_displayed);
    // — location data —
    ProError(*ProDimensionLocationGet)       (ProDimension* dim, ProView view,
        ProDrawing drawing, ProDimlocation* data);
    ProError(*ProDimlocationFree)            (ProDimlocation data);
    ProError(*ProDimlocationTextGet)         (ProDimlocation data, ProBoolean* has_elbow,
        ProPoint3d pnt, double* elbow_length);
    ProError(*ProDimlocationArrowsGet)       (ProDimlocation data,
        ProPoint3d arrow_1, ProPoint3d arrow_2);
    ProError(*ProDimlocationWitnesslinesGet) (ProDimlocation data,
        ProPoint3d witness_line_1, ProPoint3d witness_line_2);
    ProError(*ProDimlocationZExtensionlinesGet)(ProDimlocation data,
        ProBoolean* has_z_ext_1, ProPoint3d z_ext_1,
        ProBoolean* has_z_ext_2, ProPoint3d z_ext_2);
    ProError(*ProDimlocationNormalGet)       (ProDimlocation location, ProVector normal);
    ProError(*ProDimlocationCenterleadertypeGet)(ProDimlocation data,
        ProDimCenterLeaderType* center_leader_type,
        ProLeaderType* leader_type,
        double* elbow_length, ProVector elbow_direction);
    // — dimension properties —
    ProError(*ProDimensionNomvalueGet)    (ProDimension* dim, double* nominal_value);
    ProError(*ProDimensionValueGet)       (ProDimension* dimension, double* value);
    ProError(*ProDimensionDisplayedValueGet)(ProDimension* dimension, double* value);
    ProError(*ProDimensionSymbolGet)      (ProDimension* dimension, ProName symbol);
    ProError(*ProDimensionSymtextGet)     (ProDimension* dim, ProLine** r_text);
    ProError(*ProDimensionToltypeGet)     (ProDimension* dimension, ProDimToleranceType* type);
    ProError(*ProDimensionToleranceGet)   (ProDimension* dimension,
        double* upper_limit, double* lower_limit);
    ProError(*ProDimensionDisplayedToleranceGet)(ProDimension* dimension,
        double* upper_limit, double* lower_limit);
    ProError(*ProDimensionTypeGet)        (ProDimension* dimension, ProDimensiontype* type);
    ProError(*ProDimensionIsFractional)   (ProDimension* dimension, ProBoolean* fractional);
    ProError(*ProDimensionDecimalsGet)    (ProDimension* dimension, int* decimals);
    ProError(*ProDimensionDenominatorGet) (ProDimension* dimension, int* denominator);
    ProError(*ProDimensionIsReldriven)    (ProDimension* dimension, ProBoolean* rel_driven);
    ProError(*ProDimensionIsRegenednegative)(ProDimension* dimension, ProBoolean* regened_negative);
    ProError(*ProDimensionTextGet)        (ProDimension* dimension, ProLine** p_text);
    ProError(*ProDimensionTextWstringsGet)(ProDimension* dimension, wchar_t*** p_text);
    ProError(*ProDimensionPrefixGet)      (ProDimension* dimension, ProLine prefix);
    ProError(*ProDimensionSuffixGet)      (ProDimension* dimension, ProLine suffix);
    ProError(*ProDimensionIsToleranceDisplayed)(ProDimension* dimension, ProBoolean* tolerance_displayed);
    ProError(*ProDimensionIsBasic)        (ProDimension* dimension, ProBoolean* basic);
    ProError(*ProDimensionIsInspection)   (ProDimension* dimension, ProBoolean* inspection);
    ProError(*ProDimensionFeatureGet)     (ProDimension* dimension, ProFeature* feature);
    ProError(*ProDimensionOwnerfeatureGet)(ProDimension* dimension, ProFeature* feature);
    ProError(*ProDimensionIsDisplayRoundedValue)(ProDimension* dimension, ProBoolean* p_is_display_rounded);
    ProError(*ProDimensionTolerancedecimalsGet)(ProDimension* dimension, int* tolerance_decimals);
    ProError(*ProDimensionTolerancedenominatorGet)(ProDimension* dimension, int* tolerance_denominator);
    ProError(*ProDimensionOverridevalueGet)(ProDimension* dimension, double* override_value);
    ProError(*ProDimensionValuedisplayGet)(ProDimension* dimension, ProDimensionValueDisplay* value_display);
    ProError(*ProDimensionIsAccessibleInModel)(ProDimension* dimension, ProBoolean* is_accessible);
    ProError(*ProDimensionEnvelopeGet)    (ProDimension* dimension, ProDrawing drawing,
        int line_number, ProLineEnvelope envelope);
    ProError(*ProDimensionChamferLeaderGet)(ProDimension* dimension, ProDrawing drawing,
        ProDimChamferLeaderStyle* chamfer_leader_style);
    ProError(*ProDimensionChamferTextGet) (ProDimension* dimension, ProDrawing drawing,
        ProDimChamferStyle* chamfer_style);
    ProError(*ProDimensionDualOptionsGet) (ProDimension* dimension, ProDrawing drawing,
        ProDualDimensionDisplayType* type, ProName secondary_unit,
        int* dim_decimals, int* tol_decimals);
    ProError(*ProDimensionOriginSideGet)  (ProDimension* dimension, int* dim_side);
    ProError(*ProDimensionAdditionalRefsGet)(ProDimension* dim,
        ProDimensionReferenceType type,
        ProAnnotationReference** refs);
    ProError(*ProDimensionDisplayFormatGet)(ProDimension* dimension, ProDimensionDisplayFormat* type);
    ProError(*ProDimensionParentGet)      (ProDimension* dim, ProDimension* parent_dim);
    ProError(*ProDimensionIsSignDriven)   (ProDimension* dimension, ProBoolean* is_sign_driven);
    ProError(*ProDimensionOrdinatestandardGet)(ProDimension* dimension, ProDimOrdinateStandard* ordinate_standard);
    ProError(*ProDimensionBoundGet)       (ProDimension* dim, ProDimbound* r_bound);
    ProError(*ProDimensionTollabelGet)    (ProDimension* dimension, ProToleranceTable* type,
        ProName table, int* column);
    ProError(*ProDimensionCanRegenerate)  (ProDimension* dimension, ProDrawing drawing, ProBoolean* can_regen);
    ProError(*ProDimensionBaselinedirectionGet)(ProDimension* dimension, ProVector dir_vec);
    ProError(*ProDimensionTangentAttachGet)(ProDimension* dimension, ProDimArcTangent tangent,
        ProDimArcAttachType* type);
    ProError(*ProDimensionConfigGet)      (ProDimension* dimension, ProDrawing drawing,
        ProDimensionconfig* config);
    // — solid-scope dimension visitor —
    ProError(*ProSolidDimensionVisit)     (ProSolid solid, ProBoolean refdim,
        ProDimensionVisitAction action,
        ProDimensionFilterAction filter, ProAppData data);

    // ════════════════════════════════════════════════════════════════════════
    // §7  DETAIL NOTES
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingDtlnotesCollect) (ProDrawing drawing, ProDtlsymdef* symbol,
        int sheet, ProDtlnote** notes);
    ProError(*ProDrawingDtlnoteVisit)    (ProDrawing drawing, ProDtlsymdef* symbol, int sheet,
        ProDtlitemVisitAction visit_action,
        ProDtlitemFilterAction filter_action, ProAppData appdata);
    ProError(*ProDtlnoteDataGet)         (ProDtlnote* note, ProDtlsymdef* symbol,
        ProDisplayMode mode, ProDtlnotedata* notedata);
    ProError(*ProDtlnoteLineEnvelopeGet) (ProDtlnote* note, int line, ProVector envel[4]);
    ProError(*ProDtlnotedataLeadersCollect)(ProDtlnotedata notedata, ProDtlattach** leaders);
    ProError(*ProDtlnotedataLinesCollect)(ProDtlnotedata notedata, ProDtlnoteline** lines);
    ProError(*ProDtlnotedataAttachmentGet)(ProDtlnotedata notedata, ProDtlattach* attachment);
    ProError(*ProDtlnotedataIdGet)       (ProDtlnotedata notedata, int* id);
    ProError(*ProDtlnotedataIsDisplayed) (ProDtlnotedata data, ProBoolean* is_displayed);
    ProError(*ProDtlnotedataElbowlengthGet)(ProDtlnotedata data, ProBoolean* is_default, double* elbow_length);
    ProError(*ProDtlnotedataReadonlyGet) (ProDtlnotedata notedata, ProBoolean* read_only);
    ProError(*ProDtlnotedataTextStyleGet)(ProDtlnotedata note_data, ProTextStyle* r_text_style);
    ProError(*ProDtlnoteModelrefGet)     (ProDtlnote* note, ProDtlsyminst* symbol_inst,
        int line_index, int text_index, ProMdl* model);
    ProError(*ProDtlnoteNoteGet)         (ProDtlnote* dtl_note, ProNote* solid_model_note);
    ProError(*ProDtlnoteGtolGet)         (ProDtlnote* dtl_note, ProGtol* gtol);
    ProError(*ProDtlnoteDtlsyminstsCollect)(ProDtlnote* note, ProDtlsyminst** instances);
    ProError(*ProDtlnoteTableCellGet)    (ProDtlnote* note, ProDwgtable* table, int* p_row, int* p_col);
    ProError(*ProDtlnoteWrapTextGet)     (ProDtlnote* note, ProBoolean* wrap, double* wrap_width);
    ProError(*ProDtlnotelineTextsCollect)(ProDtlnoteline line, ProDtlnotetext** text);
    ProError(*ProDtlnotetextStringGet)   (ProDtlnotetext text, ProLine string);
    ProError(*ProDtlnotetextUlineGet)    (ProDtlnotetext text, ProBoolean* underline);
    ProError(*ProDtlnotetextStyleGet)    (ProDtlnotetext note_text, ProTextStyle* r_text_style);
    // — memory management —
    ProError(*ProDtlnotedataAlloc)       (ProMdl owner, ProDtlnotedata* notedata);
    ProError(*ProDtlnotedataFree)        (ProDtlnotedata notedata);
    ProError(*ProDtlnotetextAlloc)       (ProDtlnotetext* text);
    ProError(*ProDtlnotetextFree)        (ProDtlnotetext text);
    ProError(*ProDtlnotelineAlloc)       (ProDtlnoteline* line);
    ProError(*ProDtlnotelineFree)        (ProDtlnoteline line);

    // ════════════════════════════════════════════════════════════════════════
    // §8  ATTACHMENTS  (ProDtlattach.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDtlattachGet)           (ProDtlattach attachment, ProDtlattachType* type,
        ProView* view, ProVector location,
        ProSelection* attach_point);
    ProError(*ProDtlattachArrowtypeGet)  (ProDtlattach attach, ProLeaderType* arrow_type);
    ProError(*ProDtlattachIsSuppressedGet)(ProDtlattach attach, ProBoolean* is_supp);
    ProError(*ProDtlattachAlloc)         (ProDtlattachType type, ProView view,
        ProVector location, ProSelection attach_point,
        ProDtlattach* attachment);
    ProError(*ProDtlattachFree)          (ProDtlattach attachment);

    // ════════════════════════════════════════════════════════════════════════
    // §9  GD&T  (ProGtol.h / ProGtolAttach.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProGtolDtlnoteGet)         (ProGtol* solid_model_gtol, ProDrawing drawing, ProDtlnote* dtl_note);
    ProError(*ProGtolDtlnotesCollect)    (ProGtol* solid_model_gtol, ProDrawing drawing, ProDtlnote** notes);
    ProError(*ProGtolleaderGet)          (ProGtolleader leader, ProLeaderType* type, ProSelection* attachment);
    ProError(*ProGtolValueStringGet)     (ProGtol* gtol, wchar_t** value);
    ProError(*ProGtolTypeGet)            (ProGtol* gtol, ProGtolType* type);
    ProError(*ProGtolAllOverGet)         (ProGtol* gtol, ProBoolean* all_over);
    ProError(*ProGtolAllAroundGet)       (ProGtol* gtol, ProBoolean* all_around);
    ProError(*ProGtolCompositeGet)       (ProGtol* gtol, wchar_t*** values,
        wchar_t*** primary, wchar_t*** secondary, wchar_t*** tertiary);
    ProError(*ProGtolDatumReferencesGet) (ProGtol* gtol,
        wchar_t** primary, wchar_t** secondary, wchar_t** tertiary);
    ProError(*ProGtolCompositeShareRefGet)(ProGtol* gtol, ProBoolean* share);
    ProError(*ProGtolRightTextGet)       (ProGtol* gtol, wchar_t** p_text);
    ProError(*ProGtolTopTextGet)         (ProGtol* gtol, wchar_t** above_text);
    ProError(*ProGtolBottomTextGet)      (ProGtol* gtol, wchar_t** below_text);
    ProError(*ProGtolLeftTextGet)        (ProGtol* gtol, wchar_t** left_text);
    ProError(*ProGtolPrefixGet)          (ProGtol* gtol, wchar_t** prefix);
    ProError(*ProGtolSuffixGet)          (ProGtol* gtol, wchar_t** suffix);
    ProError(*ProGtolAddlTextBoxedGet)   (ProGtol* gtol, ProGtolTextType text_type, ProBoolean* is_boxed);
    ProError(*ProGtolTextstyleGet)       (ProGtol* gtol, ProTextStyle* text_style);
    ProError(*ProGtoltextTextstyleGet)   (ProGtol* gtol, ProGtolTextType text_type, ProTextStyle* text_style);
    ProError(*ProGtolElbowlengthGet)     (ProGtol* gtol, double* elbow_length, ProVector elbow_direction);
    ProError(*ProGtolLineEnvelopeGet)    (ProGtol* note, int line_number, ProLineEnvelope envelope);
    ProError(*ProGtolRightTextEnvelopeGet)(ProGtol* note, ProLineEnvelope envelope);
    ProError(*ProGtolEnvelopeGet)        (ProGtol* gtol, ProDrawing drawing, ProLineEnvelope envelope);
    ProError(*ProGtolEnvelopeGetWithFlags)(ProGtol* gtol, ProDrawing drawing,
        ProGtolEnvelopeFlag flags, ProLineEnvelope envelope);
    ProError(*ProGtolTopModelGet)        (ProGtol* gtol, ProMdl* top_mdl);
    ProError(*ProGtolUnilateralGet)      (ProGtol* gtol, ProBoolean* unilateral_set, ProBoolean* outside);
    ProError(*ProGtolBoundaryDisplayGet) (ProGtol* gtol, ProBoolean* boundary);
    ProError(*ProGtolNameGet)            (ProGtol* gtol, wchar_t** p_name);
    ProError(*ProGtolValidate)           (ProGtol* gtol, ProGtolValidityCheckType type, ProBoolean* is_valid_gtol);
    ProError(*ProGtolReferencesGet)      (ProGtol* gtol, ProAnnotationReference** p_refs);
    ProError(*ProGtolIndicatorsGet)      (ProGtol* gtol, ProGtolIndicatorType** types,
        wchar_t*** symbols, wchar_t*** dfs);
    ProError(*ProGtolleaderZExtensionlineGet)(ProGtol* gtol, ProGtolleader* leader,
        ProBoolean* is_zextension, ProPoint3d* line_end);
    ProError(*ProGtolTopTextHorizJustificationGet)(ProGtol* gtol, ProTextHrzJustification* justification);
    ProError(*ProGtolBottomTextHorizJustificationGet)(ProGtol* gtol, ProTextHrzJustification* justification);
    ProError(*ProGtolSymbolStringGet)    (ProGtolSymbol symbol, ProSymbolFont font, wchar_t** value);
    ProError(*ProGtolMakeDimensionGet)   (ProGtol* gtol, ProDimension* dim);
    ProError(*ProGeomitemIsGtolref)      (ProGeomitem* geomitem, ProBoolean* ref_datum,
        ProBoolean* is_in_dim, ProDimension* in_dim);
    ProError(*ProGeomitemSetdatumGet)    (ProGeomitem* geomitem, ProBoolean* set, ProGtolsetdatumValue* value);
    // GD&T attach
    ProError(*ProGtolAttachGet)          (ProGtol* gtol, ProGtolAttach* r_gtol_attach);
    ProError(*ProGtolAttachTypeGet)      (ProGtolAttach gtol_attach, ProGtolAttachType* type);
    ProError(*ProGtolAttachFreeGet)      (ProGtolAttach gtol_attach, ProAnnotationPlane* plane, Pro3dPnt location);
    ProError(*ProGtolAttachLeadersGet)   (ProGtolAttach gtol_attach, ProAnnotationPlane* plane,
        ProGtolLeaderAttachType* type, ProGtolleader** leaders,
        Pro3dPnt location);
    ProError(*ProGtolAttachSuppressedLeadersGet)(ProGtolAttach gtol_attach, int* missing_leads);
    ProError(*ProGtolAttachOnDatumGet)   (ProGtolAttach gtol_attach, ProModelitem* datum);
    ProError(*ProGtolAttachOnAnnotationGet)(ProGtolAttach gtol_attach, ProAnnotation* p_annot);
    ProError(*ProGtolAttachOffsetItemGet)(ProGtolAttach gtol_attach, ProSelection* offset_ref, ProVector offset);
    ProError(*ProGtolAttachMakeDimGet)   (ProGtolAttach gtol_attach, ProAnnotationPlane* plane,
        ProDimAttachment** attachments_arr,
        ProDimSense** dsense_arr, ProDimOrient* orient_hint,
        Pro3dPnt location);
    // memory management
    ProError(*ProGtolleaderAlloc)        (ProLeaderType type, ProSelection attachment, ProGtolleader* leader);
    ProError(*ProGtolleaderFree)         (ProGtolleader* leader);
    ProError(*ProGtolleadersFree)        (ProGtolleader** leaders);
    ProError(*ProGtolAttachAlloc)        (ProMdl top_model, ProGtolAttach* r_gtol_attach);
    ProError(*ProGtolAttachFree)         (ProGtolAttach* p_gtol_attach);

    // ════════════════════════════════════════════════════════════════════════
    // §10  ANNOTATIONS  (ProAnnotation.h / ProAnnotationElem.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProAnnotationElementGet)  (ProAnnotation* annotation, ProAnnotationElem* element);
    ProError(*ProAnnotationIsInactive)  (ProAnnotation* annotation, ProBoolean* is_inactive);
    ProError(*ProAnnotationIsShown)     (ProAnnotation* annotation, ProDrawing drawing, ProBoolean* is_shown);
    ProError(*ProAnnotationIsAssociative)(ProAnnotation* annotation, ProDrawing drawing,
        ProBoolean* assoc_position,
        ProAnnotationAttachmentAssociativity* assoc_attach,
        ProBoolean* future_use);
    ProError(*ProAnnotationelemIsReadonly)(ProAnnotationElem* p_element, ProBoolean* is_readonly);
    ProError(*ProAnnotationelemHasMissingrefs)(ProAnnotationElem* element,
        ProAnnotationRefFilter type,
        ProAnnotationRefFromType source,
        ProBoolean at_least_one, ProBoolean* has_missing_refs);
    ProError(*ProAnnotationTextstyleGet)(ProAnnotation* annotation, ProDrawing drawing,
        ProAsmcomppath* comp_path, ProView view,
        ProTextStyle* text_style);
    ProError(*ProAnnotationNeedsConversion)(ProAnnotation* annotation, ProBoolean* needs_conversion);
    ProError(*ProAnnotationSecuritymarkingGet)(ProAnnotation* annotation, ProBoolean* is_secure);
    ProError(*ProAnnotationOffsetSymbolsGet)(ProAnnotation* annotation, ProDrawing drawing,
        ProDtlsyminst** syminsts);
    ProError(*ProAnnotationDesignateGet)(ProAnnotation* annotation, ProDesignateType* designate);
    ProError(*ProAnnotationReferencesInheritGet)(ProAnnotation* annotation, ProBoolean* inherit);
    ProError(*ProAnnotationReferenceIsInherit)(ProAnnotation* annotation,
        ProAnnotationReference ref, ProBoolean* inherit);
    ProError(*ProDrawingViewAnnotDispStatusGet)(ProDrawing draw, ProView view,
        ProAnnotation** p_annot_array,
        ProBoolean** p_annot_display_status_array);
    // annotation planes
    ProError(*ProAnnotationplaneReferenceGet)(ProAnnotationPlane* plane, ProSelection* reference);
    ProError(*ProAnnotationplanePlaneGet)    (ProAnnotationPlane* plane, ProPlanedata* data);
    ProError(*ProAnnotationplaneVectorGet)   (ProAnnotationPlane* plane, ProVector direction);
    ProError(*ProAnnotationplaneAngleGet)    (ProAnnotationPlane* plane, ProVector orientation, double* angle);
    ProError(*ProAnnotationplaneFrozenGet)   (ProAnnotationPlane* plane, ProBoolean* frozen);
    ProError(*ProAnnotationplaneViewnameGet) (ProAnnotationPlane* plane, ProLine view_name);
    ProError(*ProAnnotationplaneTypeGet)     (ProAnnotationPlane* plane, ProAnnotationPlaneType* type);
    ProError(*ProAnnotationplaneActiveGet)   (ProMdl model, ProAnnotationPlane* plane);
    ProError(*ProAnnotationplaneNamesGet)    (ProAnnotationPlane* plane, wchar_t*** names);
    ProError(*ProAnnotationplaneByNameInit)  (ProMdl mdl, wchar_t* name, ProAnnotationPlane* plane);
    ProError(*ProAnnotationplaneForcetoplaneflagGet)(ProAnnotationPlane* plane, ProBoolean* force_to_plane);

    // ════════════════════════════════════════════════════════════════════════
    // §11  DRAWING TABLES  (ProDwgtable.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingTableVisit)     (ProDrawing drawing,
        ProDwgtableVisitAction  visit_action,
        ProDwgtableFilterAction filter_action,
        ProAppData data);
    ProError(*ProDrawingTablesCollect)  (ProDrawing drawing, ProDwgtable** tables);
    ProError(*ProDwgtableColumnsCount)  (ProDwgtable* table, int* n_columns);
    ProError(*ProDwgtableRowsCount)     (ProDwgtable* table, int* n_rows);
    ProError(*ProDwgtableSegCount)      (ProDwgtable* p_table, int* n_segs);
    ProError(*ProDwgtableSegSheetGet)   (ProDwgtable* p_table, int segment, int* sheet);
    ProError(*ProDwgtableCellNoteGet)   (ProDwgtable* table, int column, int row, ProDtlnote* note);
    ProError(*ProDwgtableCelltextGet)   (ProDwgtable* table, int column, int row,
        ProParamMode mode, ProWstring** lines);
    ProError(*ProDwgtableIsFromFormat)  (ProDwgtable* table, ProBoolean* from_format);
    ProError(*ProDwgtableInfoGet)       (ProDwgtable* table, int segment, ProDwgtableInfo* table_info);
    ProError(*ProDwgtableRowSizeGet)    (ProDwgtable* table, int segment, int row, double* size);
    ProError(*ProDwgtableColumnSizeGet) (ProDwgtable* table, int segment, int column, double* size);
    ProError(*ProDwgtableSegExtentsGet) (ProDwgtable* table, int segment_id,
        int* first_row, int* last_row,
        int* first_column, int* last_column);
    ProError(*ProDwgtableCellIsComment) (ProDwgtable* table, int column, int row, ProBoolean* is_comment);
    ProError(*ProDwgtableCellComponentGet)(ProDwgtable* table, int column, int row,
        ProAsmcomppath* component);
    ProError(*ProDwgtableCellRefmodelGet)(ProDwgtable* table, int column, int row,
        ProAssembly* assembly, ProMdl* model);
    ProError(*ProDwgtableCellRegionGet) (ProDrawing pro_drawing, ProDwgtable* pro_table,
        int column, int row, int* r_region_id);
    ProError(*ProDwgtableGrowthdirectionGet)(ProDwgtable* table, ProDwgtableGrowthdirType* p_dir);
    ProError(*ProDwgtableRowheightAutoadjustGet)(ProDwgtable* table, int row,
        ProDwgtableRowheightAutoadjusttype* auto_adjust);
    ProError(*ProDrawingTablesUpdate)   (ProDrawing drawing);

    // ════════════════════════════════════════════════════════════════════════
    // §12  3D ANNOTATION TABLES  (ProTable.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProTableVisit)            (ProMdl model,
        ProTableVisitAction  visit_action,
        ProTableFilterAction filter_action, ProAppData data);
    ProError(*ProTablesCollect)         (ProMdl model, ProTable** tables);
    ProError(*ProTableRowsColumnsCount) (ProTable* table, int* n_rows, int* n_columns);
    ProError(*ProTableCellNoteGet)      (ProTable* table, int column, int row, int* id);
    ProError(*ProTableCelltextGet)      (ProTable* table, int column, int row,
        ProTableParamMode mode, ProWstring** lines);
    ProError(*ProTableIsFromFormat)     (ProTable* table, ProBoolean* from_format);
    ProError(*ProTableColumnWidthGet)   (ProTable* table, int column,
        ProTableSizetype size_type, double* column_width);
    ProError(*ProTableRowHeightGet)     (ProTable* table, int row,
        ProTableSizetype size_type, double* row_height);
    ProError(*ProTableCellMergeGet)     (ProTable* table, int row, int col, ProBoolean* is_merge,
        int* start_row, int* start_col, int* end_row, int* end_col);
    ProError(*ProTableCellIsComment)    (ProTable* table, int column, int row, ProBoolean* is_comment);
    ProError(*ProTableAnnotationPlaneGet)(ProTable* table, ProAnnotationPlane* plane);
    ProError(*ProTableOriginGet)        (ProTable* table, int segment, ProPoint3d origin);
    ProError(*ProTableReferencesGet)    (ProTable* table, ProAnnotationReference** p_refs);
    ProError(*ProTableCellBlankGet)     (ProTable* table, int row, int col, int* blank_flags);
    ProError(*ProTableBorderStyleGet)   (ProTable* table, ProLinestyle* line_style,
        double* width, ProColor* clr);
    ProError(*ProTableCellFillColorGet) (ProTable* table, int row, int col, ProColor* color);

    // ════════════════════════════════════════════════════════════════════════
    // §13  DRAFT ENTITIES  (ProDtlentity.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingDtlentitiesCollect)(ProDrawing drawing, ProDtlsymdef* symbol,
        int sheet, ProDtlentity** entities);
    ProError(*ProDrawingDtlentityVisit) (ProDrawing drawing, ProDtlsymdef* symbol, int sheet,
        ProDtlitemVisitAction  visit_action,
        ProDtlitemFilterAction filter_action, ProAppData appdata);
    ProError(*ProDtlentityDataGet)      (ProDtlentity* entity, ProDtlsymdef* symbol,
        ProDtlentitydata* entdata);
    ProError(*ProDtlentitydataAlloc)    (ProMdl owner, ProDtlentitydata* entdata);
    ProError(*ProDtlentitydataFree)     (ProDtlentitydata entdata);
    ProError(*ProDtlentitydataIdGet)    (ProDtlentitydata entdata, int* id);
    ProError(*ProDtlentitydataCurveGet) (ProDtlentitydata entdata, ProCurvedata* curve);
    ProError(*ProDtlentitydataColorGet) (ProDtlentitydata entdata, ProColor* color);
    ProError(*ProDtlentitydataFontGet)  (ProDtlentitydata entdata, ProName font);
    ProError(*ProDtlentitydataWidthGet) (ProDtlentitydata entdata, double* width);
    ProError(*ProDtlentitydataViewGet)  (ProDtlentitydata entdata, ProView* view);
    ProError(*ProDtlentitydataIsConstruction)(ProDtlentitydata data, ProBoolean* is_construction);
    ProError(*ProDtlentitydataIsPeriodic)(ProDtlentitydata data, ProBoolean* is_periodic);
    ProError(*ProDtlentityIsOLEObject)  (ProDtlentity* entity, ProBoolean* is_ole_object);

    // ════════════════════════════════════════════════════════════════════════
    // §14  DETAIL GROUPS  (ProDtlgroup.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingDtlgroupsCollect)(ProDrawing drawing, int sheet, ProDtlgroup** groups);
    ProError(*ProDrawingDtlgroupVisit)  (ProDrawing drawing, int sheet,
        ProDtlitemVisitAction  visit_action,
        ProDtlitemFilterAction filter_action, ProAppData appdata);
    ProError(*ProDtlgroupDataGet)       (ProDtlgroup* group, ProDtlgroupdata* data);
    ProError(*ProDtlgroupdataFree)      (ProDtlgroupdata groupdata);
    ProError(*ProDtlgroupdataIdGet)     (ProDtlgroupdata groupdata, int* id);
    ProError(*ProDtlgroupdataNameGet)   (ProDtlgroupdata groupdata, ProName name);
    ProError(*ProDtlgroupdataItemsCollect)(ProDtlgroupdata groupdata, ProDtlitem** items);
    ProError(*ProDtlgroupdataIsDisplayed)(ProDtlgroupdata data, ProBoolean* is_displayed);

    // ════════════════════════════════════════════════════════════════════════
    // §15  SYMBOL INSTANCES  (ProDtlsyminst.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingDtlsyminstsCollect)(ProDrawing drawing, int sheet, ProDtlsyminst** syminsts);
    ProError(*ProDrawingDtlsyminstVisit)(ProDrawing drawing, int sheet,
        ProDtlitemVisitAction  visit_action,
        ProDtlitemFilterAction filter_action, ProAppData appdata);
    ProError(*ProDtlsyminstDataGet)     (ProDtlsyminst* syminst, ProDisplayMode mode,
        ProDtlsyminstdata* data);
    ProError(*ProDtlsyminstdataAlloc)   (ProMdl model, ProDtlsyminstdata* data);
    ProError(*ProDtlsyminstdataFree)    (ProDtlsyminstdata data);
    ProError(*ProDtlsyminstDimattachGet)(ProDtlsyminst* syminst, ProDimension* dim);
    ProError(*ProDtlsyminstdataColorGet)(ProDtlsyminstdata data, ProColor* color);
    ProError(*ProDtlsyminstdataDefGet)  (ProDtlsyminstdata data, ProDtlsymdef* definition);
    ProError(*ProDtlsyminstdataAttachtypeGet)(ProDtlsyminstdata data, ProDtlsymdefattachType* type);
    ProError(*ProDtlsyminstdataAttachmentGet)(ProDtlsyminstdata data, ProDtlattach* attachment);
    ProError(*ProDtlsyminstdataLeadersCollect)(ProDtlsyminstdata data, ProDtlattach** leaders);
    ProError(*ProDtlSyminstElbowlengthGet)(ProDtlsyminst* p_sym_inst,
        double* op_elbow_length, ProVector elbow_direction);
    ProError(*ProDtlsyminstdataElbowlengthGet)(ProDtlsyminstdata data,
        ProBoolean* is_default, double* elbow_length);
    ProError(*ProDtlsyminstdataAngleGet)(ProDtlsyminstdata data, double* angle);
    ProError(*ProDtlsyminstdataHeightGet)(ProDtlsyminstdata data, double* height);
    ProError(*ProDtlsyminstdataVartextsCollect)(ProDtlsyminstdata data, ProDtlvartext** vartexts);
    ProError(*ProDtlsyminstdataTransformGet)(ProDtlsyminstdata data, ProMatrix transform);
    ProError(*ProDtlsyminstdataScaledheightGet)(ProDtlsyminstdata data, double* height);
    ProError(*ProDtlvartextAlloc)       (ProLine prompt, ProLine value, ProDtlvartext* vartext);
    ProError(*ProDtlvartextFree)        (ProDtlvartext vartext);
    ProError(*ProDtlvartextDataGet)     (ProDtlvartext vartext, ProLine prompt, ProLine value);
    ProError(*ProDtlsyminstdataIsDisplayed)(ProDtlsyminstdata data, ProBoolean* is_displayed);
    ProError(*ProDtlsyminstdataIsInvisible)(ProDtlsyminstdata data, ProBoolean* is_invisible);
    ProError(*ProDtlsyminstdataIdGet)   (ProDtlsyminstdata data, int* id);
    ProError(*ProDtlsyminstSymgroupsCollect)(ProDtlsyminst* sym_inst,
        ProDtlsyminstGroupStatus status,
        ProDtlsymgroup** group_array);
    ProError(*ProDtlsyminstFeatureGet)  (ProDtlsyminst* symbol_instance, ProFeature* symbol_owner);
    ProError(*ProDtlsyminstEntitiesVisibleGet)(ProDtlsyminst* sym_inst, int** status);
    ProError(*ProDtlsyminstEnvelopeGet) (ProDtlsyminst* syminst, ProLineEnvelope envelope);
    ProError(*ProDtlsyminstReferencesGet)(ProDtlsyminst* syminst, ProAnnotationReference** p_refs);
    ProError(*ProDtlsyminstSolidSymGet) (ProDtlsyminst* syminst, ProDtlsyminst* solid_sym);

    // ════════════════════════════════════════════════════════════════════════
    // §16  LAYERS  (ProLayer.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProMdlLayerVisit)         (ProMdl model,
        ProLayerAction visit_action,
        ProLayerAction filter_action, ProAppData app_data);
    ProError(*ProMdlLayersVisit)        (ProMdl model,
        ProLayerAction visit_action,
        ProLayerFilter filter_action, ProAppData app_data);
    ProError(*ProMdlLayerGet)           (ProMdl owner, ProName layer_name, ProLayer* layer);
    ProError(*ProMdlLayersCollect)      (ProMdl mdl, ProLayer** p_layers_array);
    ProError(*ProLayerItemsGet)         (ProLayer* layer, ProLayerItem** p_layeritem, int* p_count);
    ProError(*ProLayerItemsPopulate)    (ProLayer* layer, ProLayerItem** p_layeritem, int* p_count);
    ProError(*ProLayeritemarrayFree)    (ProLayerItem** array);
    ProError(*ProLayerDisplaystatusGet) (ProLayer* layer, ProLayerDisplay* display_status);
    ProError(*ProLayerSavedstatusGet)   (ProLayer* layer, ProLayerDisplay* saved_status);
    ProError(*ProDwgLayerSavedstatusGet)(ProLayer* layer, ProView view, ProLayerDisplay* saved_status);
    ProError(*ProDwgLayerDisplaystatusGet)(ProLayer* layer, ProView view, ProLayerDisplay* display_status);
    ProError(*ProLayerViewDependencyGet)(ProView view, ProBoolean* depend);
    ProError(*ProLayerDefLayerGet)      (ProDefLayerType def_layer_type, ProName def_layer_name);
    ProError(*ProLayeritemStatusGet)    (ProLayerItem* pro_layer_item, ProView pro_view,
        ProBoolean* p_item_visible);
    ProError(*ProLayeritemLayerStatusGet)(ProDrawing pro_drawing, ProLayerItem* pro_layer_item,
        ProLayer* pro_layer, ProLayerItemStatus* pro_layitem_status);
    ProError(*ProLayeritemLayersGet)    (ProDrawing pro_drawing, ProLayerItem* pro_layer_item,
        ProLayer** pro_layers);
    ProError(*ProModelitemIsHidden)     (ProModelitem* item, ProBoolean* is_hidden);

    // ════════════════════════════════════════════════════════════════════════
    // §17  SOLID / ASSEMBLY  (ProSolid.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProSolidFeatVisit)        (ProSolid p_handle,
        ProFeatureVisitAction  visit_action,
        ProFeatureFilterAction filter_action, ProAppData app_data);
    ProError(*ProSolidAxisVisit)        (ProSolid p_handle,
        ProAxisVisitAction   visit_action,
        ProAxisFilterAction  filter_action, ProAppData app_data);
    ProError(*ProSolidCsysVisit)        (ProSolid p_handle,
        ProCsysVisitAction   visit_action,
        ProCsysFilterAction  filter_action, ProAppData app_data);
    ProError(*ProSolidSurfaceVisit)     (ProSolid p_handle,
        ProSurfaceVisitAction  visit_action,
        ProSurfaceFilterAction filter_action, ProAppData app_data);
    ProError(*ProSolidDispoutlineGet)   (ProSolid solid, ProMatrix transform,
        double r_outline_points[2][3]);
    ProError(*ProSolidOutlineGet)       (ProSolid p_solid, Pro3dPnt r_outline_points[2]);
    ProError(*ProSolidMassPropertyGet)  (ProSolid solid, ProName csys_name, ProMassProperty* mass_prop);
    ProError(*ProSolidAccuracyGet)      (ProSolid solid, ProAccuracyType* r_type, double* r_accuracy);
    ProError(*ProSolidToleranceGet)     (ProSolid solid, ProToleranceType type,
        int n_decimals, double* tolerance);
    ProError(*ProSolidModelclassGet)    (ProSolid solid, ProModelClass* model_class);
    ProError(*ProSolidDefaulttextheightGet)(ProSolid solid, double* default_text_height);
    ProError(*ProSolidToleranceStandardGet)(ProSolid p_solid, ProStandard* p_standard);
    ProError(*ProSolidFeatstatusGet)    (ProSolid solid, int** p_feat_id_array,
        ProFeatStatus** p_status_array, int* p_num_features);
    ProError(*ProSolidMaxsizeGet)       (ProSolid p_solid, double* r_max_size);

    // ════════════════════════════════════════════════════════════════════════
    // §18  SELECTION  (ProSelection.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProSelectionAlloc)        (ProAsmcomppath* p_cmp_path, ProModelitem* p_mdl_itm,
        ProSelection* p_selection);
    ProError(*ProSelectionCopy)         (ProSelection from_selection, ProSelection* p_to_selection);
    ProError(*ProSelectionFree)         (ProSelection* p_selection);
    ProError(*ProSelectionarrayFree)    (ProSelection* sel_array);
    ProError(*ProSelectionSet)          (ProSelection selection, ProAsmcomppath* p_cmp_path,
        ProModelitem* p_mdl_itm);
    ProError(*ProSelectionAsmcomppathGet)(ProSelection selection, ProAsmcomppath* p_cmp_path);
    ProError(*ProSelectionModelitemGet) (ProSelection selection, ProModelitem* p_mdl_item);
    ProError(*ProSelectionViewGet)      (ProSelection selection, ProView* p_view);
    ProError(*ProSelectionPoint3dGet)   (ProSelection selection, ProPoint3d point);
    ProError(*ProSelectionDwgtblcellGet)(ProSelection selection,
        int* table_segment, int* row, int* column);
    ProError(*ProSelectionDwgtableGet)  (ProSelection selection, ProDwgtable* table);
    ProError(*ProSelectionDrawingGet)   (ProSelection selection, ProDrawing* drawing);
    ProError(*ProSelectionVerify)       (ProSelection selection);

    // ════════════════════════════════════════════════════════════════════════
    // §19  ARRAY UTILITIES  (ProArray.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProArraySizeGet)          (ProArray arr, int* p_size);
    ProError(*ProArrayFree)             (ProArray* p_arr);
    ProError(*ProArrayAlloc)            (int n_objs, int obj_size,
        int reallocation_size, ProArray* p_array);

    // ════════════════════════════════════════════════════════════════════════
    // §21  TEXT STYLE  (ProNote.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProTextStyleAlloc)                (ProTextStyle* r_text_style);
    ProError(*ProTextStyleFree)                 (ProTextStyle* p_text_style);
    ProError(*ProTextStyleHeightGet)            (ProTextStyle text_style, double* r_height);
    ProError(*ProTextStyleWidthGet)             (ProTextStyle text_style,
        double* r_width_factor);
    ProError(*ProTextStyleAngleGet)             (ProTextStyle text_style, double* r_angle);
    ProError(*ProTextStyleSlantAngleGet)        (ProTextStyle text_style,
        double* r_slant_angle);
    ProError(*ProTextStyleThicknessGet)         (ProTextStyle text_style,
        double* r_thickness);
    ProError(*ProTextStyleUnderlineGet)         (ProTextStyle text_style, int* r_underline);
    ProError(*ProTextStyleMirrorGet)            (ProTextStyle text_style, int* r_mirror);
    ProError(*ProTextStyleJustificationGet)     (ProTextStyle text_style,
        ProTextHrzJustification* justification);
    ProError(*ProTextStyleColorGetWithDef)      (ProTextStyle text_style, ProColor* color);
    ProError(*ProTextStyleIsHeightInModelUnits) (ProTextStyle text_style,
        ProBoolean* is_height_in_model_units);

    // ════════════════════════════════════════════════════════════════════════
    // §24  SYMBOL DEFINITIONS  (ProDtlsymdef.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDtlsymdefDataGet)          (ProDtlsymdef* symdef,
        ProDtlsymdefdata* data);
    ProError(*ProDtlsymdefdataIdGet)        (ProDtlsymdefdata symdefdata, int* id);
    ProError(*ProDtlsymdefdataNameGet)      (ProDtlsymdefdata symdefdata, ProName name);
    ProError(*ProDtlsymdefdataPathGet)      (ProDtlsymdefdata symdefdata, ProPath path);
    ProError(*ProDtlsymdefdataHeighttypeGet)(ProDtlsymdefdata symdefdata,
        ProDtlsymdefdataHeighttype* type);
    ProError(*ProDtlsymdefdataScaledheightGet)(ProDtlsymdefdata symdefdata,
        double* height);
    ProError(*ProDtlsymdefdataElbowGet)     (ProDtlsymdefdata symdefdata,
        ProBoolean* elbow);
    ProError(*ProDtlsymdefdataTextangfixedGet)(ProDtlsymdefdata symdefdata,
        ProBoolean* text_angle_fixed);
    ProError(*ProDtlsymdefdataAttachGet)    (ProDtlsymdefdata symdefdata,
        ProDtlsymdefattach** attaches);
    ProError(*ProDtlsymdefdataTextrefGet)   (ProDtlsymdefdata symdefdata,
        int* text_entity, int* text_line,
        int* text_text);
    ProError(*ProDtlsymdefattachGet)        (ProDtlsymdefattach attach,
        ProDtlsymdefattachType* type,
        int* entity_id,
        double* entity_parameter,
        ProVector location);
    ProError(*ProDtlsymgroupDataGet)        (ProDtlsymgroup* group_level,
        ProDtlsymgroupdata* data);
    ProError(*ProDtlsymgroupdataNameGet)    (ProDtlsymgroupdata symgroupdata,
        ProName group_name);
    ProError(*ProDtlsymgroupdataItemsCollect)(ProDtlsymgroupdata data,
        ProDtlitem** item_array);
    ProError(*ProDtlsymgroupSubgroupsCollect)(ProDtlsymgroup* parent_group,
        ProDtlsymgroup** group_array);
    ProError(*ProDtlsymgroupParentGet)      (ProDtlsymgroup* group,
        ProDtlsymgroup* parent_group);
    ProError(*ProDtlsyminstdataDefattachGet)(ProDtlsyminstdata data,
        ProDtlsymdefattach* attach);
    ProError(*ProDrawingDtlsymdefsCollect)  (ProDrawing drawing,
        ProDtlsymdef** symdefs);
    ProError(*ProDrawingDtlsymdefVisit)     (ProMdl model,
        ProDtlitemVisitAction  visit_action,
        ProDtlitemFilterAction filter_action,
        ProAppData appdata);

    // ════════════════════════════════════════════════════════════════════════
    // §26  ADDITIONAL DRAWING FUNCTIONS
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProDrawingDimensionPathGet)    (ProDrawing drawing,
        ProDimension* dimension,
        ProAsmcomppath* path);
    ProError(*ProDrawingToleranceStandardVersionGet)(ProDrawing p_draw,
        ProStandardVersion* version);
    ProError(*ProDrawingEdgeDisplayGet)      (ProSelection edge_sel,
        ProDrawingEdgeDisplay* edge_display);
    ProError(*ProDrawingViewXhatchDependentGet)(ProDrawing drawing, ProView view,
        ProBoolean* is_dep);
    ProError(*ProDrawingOLEobjectsVisit)     (ProDrawing drawing,
        ProModelitemVisitAction  action,
        ProModelitemFilterAction filter,
        ProAppData app_data);
    ProError(*ProDrawingOLEobjectOutlineGet) (ProModelitem* ole_object,
        ProPoint3d outline[2]);
    ProError(*ProDrawingOLEobjectSheetGet)   (ProModelitem* ole_object, int* sheet);

    // ════════════════════════════════════════════════════════════════════════
    // §27  ADDITIONAL SOLID / FEATURE FUNCTIONS
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProFeatureDimensionVisit)      (ProFeature* feature,
        ProDimensionVisitAction  visit,
        ProDimensionFilterAction filter,
        ProAppData data);
    ProError(*ProFeatureStatusGet)           (ProFeature* p_feat_handle,
        ProFeatStatus* p_status);
    ProError(*ProFeatureTypeGet)             (ProFeature* p_feat_handle,
        ProFeattype* p_type);
    ProError(*ProFeatureDtlsyminstGet)       (ProFeature* symbol_owner,
        ProDrawing drawing,
        ProDtlsyminst* symbol_instance);
    ProError(*ProFeatureSelectionGet)        (ProFeature* p_feature,
        ProSelection* p_selection);
    ProError(*ProFeatureVisibilityGet)       (ProFeature* p_feat_handle,
        ProBoolean* p_visible);
    ProError(*ProSolidToleranceStandardVersionGet)(ProSolid p_solid,
        ProStandardVersion* version);
    ProError(*ProSolidRegenerationstatusGet) (ProSolid solid,
        ProSolidRegenerationStatus* regen_status);

    // ════════════════════════════════════════════════════════════════════════
    // §28  ADDITIONAL SELECTION  (ProDimension.h / ProTable.h / ProDtlsyminst.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProSelectionDimArrowGet)       (ProSelection selection,
        ProDimension* dimension,
        int* wline_side, ProPoint3d location);
    ProError(*ProSelectionDimWitnessLineGet) (ProSelection selection,
        ProDimension* dimension,
        int* wline_side, ProPoint3d location);
    ProError(*ProSelectionDtlsyminstEntityGet)(ProSelection* selection,
        ProDtlsyminst* symbol,
        int* entity_id, double* param);
    ProError(*ProSelectionTableGet)          (ProSelection selection, ProTable* table);

    // ════════════════════════════════════════════════════════════════════════
    // §29  SIMPLIFIED REPRESENTATIONS  (ProSimprep.h / ProSimprepdata.h)
    // ════════════════════════════════════════════════════════════════════════
    ProError(*ProSimprepActiveGet)           (ProSolid solid, ProSimprep* p_simp_rep);
    ProError(*ProSimprepTypeGet)             (ProSimprep* p_simp_rep,
        ProSimprepType* p_type);
    ProError(*ProSimprepdataGet)             (ProSimprep* p_simp_rep,
        ProSimprepdata** p_data);
    ProError(*ProSimprepdataNameGet)         (ProSimprepdata* p_data, ProName name);
};
