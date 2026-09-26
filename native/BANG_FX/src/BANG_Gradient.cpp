// BANG_Gradient.cpp — BANG Gradient
//  정지점 최대 8개짜리 그라데이션을 레이어 위에 그린다. 자세한 설계 의도는 헤더 주석 참고.
//  · 모양마다 화면 좌표 → 0~1 파라미터 t 를 구하고, t 를 정지점 목록으로 색·불투명도로 바꾼다.
//  · Contour 는 알파 경계까지의 거리(BANG Stroke 와 같은 Felzenszwalb EDT)를 t 로 쓴다.
//  · 보간은 sRGB / Linear / OKLab / OKLCh 중 하나. OKLab 계열은 중간에서 밝기가 꺼지지 않는다.
//  · Dither 는 8bpc 로 떨어질 때 생기는 띠를 없애려고 t 에 ±0.5LSB 크기의 잡음을 준다.
//  · AE 이펙트 버퍼는 straight alpha — 합성은 premultiplied 로 누적한 뒤 마지막에 straight 로 되돌린다.

#include "BANG_Gradient.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <string>

// ── 명령 처리 ────────────────────────────────────────────────

static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
        "BANG Gradient v%d.%d\rMulti-stop gradient - BANG_Toolbox\rLinear/Radial/Angular/Diamond/Reflected/Contour, OKLab interpolation, dithering.",
        BANG_GRAD_MAJOR, BANG_GRAD_MINOR);
    return PF_Err_NONE;
}

static AEGP_PluginID g_plugin_id = 0;
static bool          g_barEdit = false;   // 색 띄·프리셋이 Stops 를 바꿨 경우(위치 재분배 생략)
static bool          g_registered = false;

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(BANG_GRAD_MAJOR, BANG_GRAD_MINOR, BANG_GRAD_BUG, BANG_GRAD_STAGE, BANG_GRAD_BUILD);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI | PF_OutFlag_CUSTOM_UI;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;
    if (!g_registered && in_data->appl_id != kAppID_Premiere) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        if (suites.UtilitySuite3()->AEGP_RegisterWithAEGP(NULL, "BANG Gradient", &g_plugin_id) == A_Err_NONE) g_registered = true;
    }
    return PF_Err_NONE;
}

// 기본 8색 (보라 → 분홍 계열). 정지점 기본 위치는 균등 분할.
//  기본 Stops 가 3 이므로 앞에서부터 세 색만으로도 그럴듯하게: 남은 다섯은 그 사이를 메운다.
// ── 프리셋 ────────────────────────────────
//  위치는 %, 색은 0~255, 불투명도는 %.
struct BG_PresetStop { float pos; A_u_char r, g, b; float op; };
struct BG_Preset { const char* name; int n; BG_PresetStop s[BG_NUM_STOPS]; };

static const BG_Preset kPresets[] = {
    { "Custom", 0, {} },
    { "Black to White", 2, { {0,0,0,0,100}, {100,255,255,255,100} } },
    { "White to Black", 2, { {0,255,255,255,100}, {100,0,0,0,100} } },
    // 밝은 띄와 어두운 띄가 번갈아 나오는 게 금속 반사의 핵심
    { "Chrome",  7, { {0,26,30,36,100}, {18,214,222,230,100}, {34,92,102,114,100}, {52,255,255,255,100},
                      {68,120,130,142,100}, {84,232,238,244,100}, {100,40,46,54,100} } },
    { "Gold",    6, { {0,74,47,0,100}, {20,255,217,122,100}, {42,169,116,0,100}, {60,255,243,196,100},
                      {80,138,90,0,100}, {100,255,225,150,100} } },
    { "Sunset",  4, { {0,42,18,72,100}, {35,183,46,120,100}, {70,247,123,58,100}, {100,255,214,120,100} } },
    { "Ocean",   4, { {0,4,24,64,100}, {38,10,90,140,100}, {72,26,170,178,100}, {100,160,240,228,100} } },
    { "Fire",    5, { {0,12,6,4,100}, {25,140,20,10,100}, {55,232,80,16,100}, {80,250,176,42,100}, {100,255,242,190,100} } },
    { "Rainbow", 7, { {0,232,48,48,100}, {17,240,160,40,100}, {33,236,226,52,100}, {50,68,200,88,100},
                      {67,56,160,232,100}, {84,96,84,216,100}, {100,208,72,200,100} } },
    { "Fade Out", 2, { {0,255,255,255,100}, {100,255,255,255,0} } },
};
static const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
static const char* kPresetMenu =
    "Custom|Black to White|White to Black|Chrome|Gold|Sunset|Ocean|Fire|Rainbow|Fade Out";

// 정지점 묶음을 파라미터에 글어넣는다 (클릭·프리셋·임포트 공통)
static void WriteStops(PF_ParamDef* params[], const BG_PresetStop* st, int n)
{
    n = std::min(BG_NUM_STOPS, std::max(2, n));
    params[BG_COUNT]->u.sd.value = n;
    params[BG_COUNT]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
    for (int i = 0; i < n; i++) {
        params[BG_SC(i)]->u.cd.value.red   = st[i].r;
        params[BG_SC(i)]->u.cd.value.green = st[i].g;
        params[BG_SC(i)]->u.cd.value.blue  = st[i].b;
        params[BG_SC(i)]->uu.change_flags  = PF_ChangeFlag_CHANGED_VALUE;
        params[BG_SP(i)]->u.fs_d.value = st[i].pos;
        params[BG_SP(i)]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        params[BG_SO(i)]->u.fs_d.value = st[i].op;
        params[BG_SO(i)]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
    }
}

static const int kBarH = 44;          // 가로 색 띄 컨트롤 높이
static const int kBarStrip = 20;      // 미리보기 띄 높이
static const int kBarChip = 16;       // 정지점 칩 크기

static const A_u_char kDefault[BG_NUM_STOPS][3] = {
    {  46,  26, 110 }, { 205,  90, 215 }, { 252, 200, 140 }, {  93,  48, 180 },
    { 150,  70, 220 }, { 240, 120, 180 }, { 250, 160, 150 }, { 255, 235, 160 },
};
static float DefaultPos(int i) { return (i < 3) ? i * 50.f : 100.f; }

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    #define LINE "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"   // ──────
    #define TOPIC_OPEN(NAME, ID)   do { AEFX_CLR_STRUCT(def); PF_ADD_TOPIC(NAME, ID); } while (0)
    #define TOPIC_CLOSED(NAME, ID) do { AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_START_COLLAPSED; PF_ADD_TOPIC(NAME, ID); } while (0)
    #define TOPIC_END(ID)          do { AEFX_CLR_STRUCT(def); PF_END_TOPIC(ID); } while (0)
    #define FSLIDER(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, ID) do { AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, 0, ID); } while (0)

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Shape", 6, BG_SHAPE_LINEAR, "Linear|Radial|Angular|Diamond|Reflected|Contour", PF_ParamFlag_SUPERVISE, BG_SHAPE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Start", 20, 20, 0, BG_START);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("End", 80, 80, 0, BG_END);
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Fit", "Fit Horizontal", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_FIT_H);
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("", "Fit Vertical", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_FIT_V);
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("", "Fit Diagonal", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_FIT_D);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Lock Gradient", FALSE, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_LOCK);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    PF_ADD_ANGLE("Angle Offset", 0, BG_ANGLE_OFF);
    FSLIDER("Contour Span (px)", 1, 5000, 1, 400, 120, PF_Precision_TENTHS, 0, BG_CONTOUR_SPAN);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Repeat", 3, BG_REPEAT_CLAMP, "Clamp|Repeat|Mirror", BG_REPEAT);
    FSLIDER("Cycles", 0.1, 50, 1, 8, 1, PF_Precision_HUNDREDTHS, 0, BG_CYCLES);
    FSLIDER("Phase", -1000, 1000, -100, 100, 0, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BG_PHASE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Reverse", FALSE, 0, BG_REVERSE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Interpolate", 5, BG_INTERP_OKLAB, "sRGB|Linear|OKLab|OKLCh (short hue)|OKLCh (long hue)", BG_INTERP);
    FSLIDER("Smoothness", 0, 100, 0, 100, 0, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BG_SMOOTH);
    TOPIC_OPEN("Gradient Colors " LINE, BG_G_STOPS);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY; def.ui_flags = PF_PUI_CONTROL;
    def.ui_width = 300; def.ui_height = kBarH;
    PF_ADD_CHECKBOX("Stops Bar", "", FALSE, 0, BG_BAR);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Stops", 2, BG_NUM_STOPS, 2, BG_NUM_STOPS, 3, BG_COUNT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Preset", kPresetCount, 1, kPresetMenu, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_PRESET);
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("", "Randomize", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_RANDOM);
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("File", "Import...", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_IMPORT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("", "Export...", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BG_EXPORT);
    for (int i = 0; i < BG_NUM_STOPS; i++) {
        char nm[32];
        sprintf_s(nm, "Stop %d", i + 1);
        TOPIC_CLOSED(nm, BG_SG(i));
        sprintf_s(nm, "Color %d", i + 1);
        AEFX_CLR_STRUCT(def);
        PF_ADD_COLOR(nm, kDefault[i][0], kDefault[i][1], kDefault[i][2], BG_SC(i));
        sprintf_s(nm, "Position %d", i + 1);
        FSLIDER(nm, 0, 100, 0, 100, DefaultPos(i), PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BG_SP(i));
        sprintf_s(nm, "Opacity %d", i + 1);
        FSLIDER(nm, 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BG_SO(i));
        TOPIC_END(BG_SGE(i));
    }
    TOPIC_END(BG_G_STOPS_END);

    TOPIC_CLOSED("Output " LINE, BG_G_OUT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Alpha", 2, BG_ALPHA_REPLACE, "Composite over original|Replace (opacity cuts out)", BG_ALPHA_MODE);
    FSLIDER("Dither", 0, 100, 0, 100, 40, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BG_DITHER);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Blend With Original", 5, BG_BLEND_NORMAL, "Normal|Multiply|Screen|Add|Overlay", BG_BLEND);
    FSLIDER("Amount", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BG_AMOUNT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Preserve Alpha", TRUE, 0, BG_PRESERVE_ALPHA);
    TOPIC_END(BG_G_OUT_END);

    #undef FSLIDER
    #undef TOPIC_OPEN
    #undef TOPIC_CLOSED
    #undef TOPIC_END
    #undef LINE

    // 커스텀 UI(ECW 이벤트) 등록
    PF_CustomUIInfo ci;
    AEFX_CLR_STRUCT(ci);
    ci.events = PF_CustomEFlag_EFFECT;
    ci.comp_ui_alignment = ci.layer_ui_alignment = ci.preview_ui_alignment = PF_UIAlignment_NONE;
    err = (*(in_data->inter.register_ui))(in_data->effect_ref, &ci);

    out_data->num_params = BG_NUM_PARAMS;
    return err;
}

// ── ECW 상태: 쓰지 않는 정지점과 모양별 항목을 회색으로 ──────
//  (회색 처리하는 곳에 '다시 켜는 컨트롤' 을 두지 않는다 — Shape·Stops 는 바깥에 있다)
// 정지점 줄 숨김/펼침. 안 쓰는 줄이 여덟 개나 남아 있으면 보기 복잡해서 아예 감춘다.
//  ⚠ **숨긴 스트림은 스크립트에서 setValue 가 안 된다** — 파일 임포트 직전에는 전부 펼쳐 둬야
//  세 번째 이후 정지점에 값이 들어간다(그러지 않으면 조용히 두 개만 들어오고 만다).
static void ApplyStopVisibility(PF_InData* in_data, int visible)
{
    if (!g_registered) return;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_EffectRefH meH = NULL;
    if (suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(g_plugin_id, in_data->effect_ref, &meH) || !meH) return;
    for (int i = 0; i < BG_NUM_STOPS; i++) {
        const A_Boolean hide = (i < visible) ? FALSE : TRUE;
        // 세 줄을 먼저 숨기고 그룹을 마지막에 — 그룹부터 숨기면 그 뒤 인덱스 조회가 어긋난다
        const A_long ix[4] = { BG_SC(i), BG_SP(i), BG_SO(i), BG_SG(i) };
        for (int k = 0; k < 4; k++) {
            AEGP_StreamRefH sH = NULL;
            if (!suites.StreamSuite2()->AEGP_GetNewEffectStreamByIndex(g_plugin_id, meH, ix[k], &sH) && sH) {
                suites.DynamicStreamSuite2()->AEGP_SetDynamicStreamFlag(sH, AEGP_DynStreamFlag_HIDDEN, FALSE, hide);
                suites.StreamSuite2()->AEGP_DisposeStream(sH);
            }
        }
    }
    suites.EffectSuite2()->AEGP_DisposeEffect(meH);
}

static PF_Err UpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    if (!g_registered) return PF_Err_NONE;
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const A_long shape = params[BG_SHAPE]->u.pd.value;
    const A_long count = params[BG_COUNT]->u.sd.value;

    auto grey = [&](int idx, PF_ParamType type, bool on) {
        PF_ParamDef copy = *params[idx];
        copy.param_type = type;
        if (on) copy.ui_flags &= ~PF_PUI_DISABLED; else copy.ui_flags |= PF_PUI_DISABLED;
        ERR2(suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, idx, &copy));
    };
    grey(BG_ANGLE_OFF,    PF_Param_ANGLE,        shape == BG_SHAPE_ANGULAR);
    grey(BG_CONTOUR_SPAN, PF_Param_FLOAT_SLIDER, shape == BG_SHAPE_CONTOUR);
    grey(BG_END,          PF_Param_POINT,        shape != BG_SHAPE_CONTOUR);
    ApplyStopVisibility(in_data, (int)count);
    return err;
}

// Start·End 를 레이어 내용(sourceRectAtTime) 에 맞추거나, 표현식으로 고정한다.
//  mode 0 = 가로 맞춤 · 1 = 세로 맞춤 · 2 = 지금 방향 그대로 잠금(Lock ON) · 3 = 잠금 해제
//  · Linear·Contour 처럼 가로지르는 모양: 내용 상자를 방향대로 가로지르게(세로면 상단·하단 가운데)
//  · Radial·Diamond·Reflected: Start = 중심, End = 꼭지점 (반지름이 상자를 덮음)
//  · Angular: Start = 중심, End = 방향 쪽 변 가운데 (0° 기준)
//  · Contour: 짧은 변의 절반을 Span 으로
//  ⚠ 셰이프·텍스트는 점 파라미터가 컴 좌표라 sourceRect(소스 좌표)에 position-anchorPoint 를 더한다.
//    회전·스케일은 이펙트 다음에 적용되므로 보정하지 않는다 — 그라데이션이 도형과 함께 돌아간다.
static PF_Err ApplyFit(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], int mode)
{
    if (!g_registered) return PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH meL = NULL;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &meL) || !meL) return PF_Err_NONE;
    AEGP_CompH compH = NULL; suites.LayerSuite9()->AEGP_GetLayerParentComp(meL, &compH);
    AEGP_ItemH itemH = NULL; A_long compId = 0;
    if (compH) { suites.CompSuite11()->AEGP_GetItemFromComp(compH, &itemH); if (itemH) suites.ItemSuite9()->AEGP_GetItemID(itemH, &compId); }
    A_long layerIdx = 0; suites.LayerSuite9()->AEGP_GetLayerIndex(meL, &layerIdx);
    AEGP_ObjectType meType = AEGP_ObjectType_AV;
    suites.LayerSuite9()->AEGP_GetLayerObjectType(meL, &meType);
    const bool vec = (meType == AEGP_ObjectType_VECTOR || meType == AEGP_ObjectType_TEXT);

    const int shape = (int)params[BG_SHAPE]->u.pd.value;
    double ux = 0, uy = 1;
    // 지금 방향(Start→End). 버튼을 다시 누를 때 반대편·다음 사분면으로 넘기려고 쓴다.
    double cux = FIX_2_FLOAT(params[BG_END]->u.td.x_value) - FIX_2_FLOAT(params[BG_START]->u.td.x_value);
    double cuy = FIX_2_FLOAT(params[BG_END]->u.td.y_value) - FIX_2_FLOAT(params[BG_START]->u.td.y_value);
    const double cul = std::sqrt(cux * cux + cuy * cuy);
    if (cul < 1e-6) { cux = 0; cuy = 1; } else { cux /= cul; cuy /= cul; }

    if (mode == 0)      { ux = (cux > 0.001) ? -1 : 1; uy = 0; }         // 좌↔우 토글
    else if (mode == 1) { ux = 0; uy = (cuy > 0.001) ? -1 : 1; }         // 상↔하 토글
    else if (mode == 4) {
        // 사분면 시계방향: ↘(45°) → ↙(135°) → ↖(225°) → ↗(315°)
        //  (화면 좌표는 y 가 아래로 가므로 각도가 커지는 쪽이 시계방향이다)
        const double PI = 3.14159265358979;
        double ang = std::atan2(cuy, cux);                                // -π ~ π
        if (ang < 0) ang += 2 * PI;
        const double q = (ang - PI / 4) / (PI / 2);
        const double frac = std::fabs(q - std::floor(q + 0.5));
        int idx = (frac < 0.06) ? ((int)std::floor(q + 0.5) + 1) : 0;     // 대각선 위면 다음 칸, 아니면 ↘ 부터
        idx = ((idx % 4) + 4) % 4;
        const double a2 = PI / 4 + idx * (PI / 2);
        ux = std::cos(a2); uy = std::sin(a2);
    }
    else { ux = cux; uy = cuy; }
    const bool lockOn = (mode == 2) || (mode != 3 && params[BG_LOCK]->u.bd.value != 0);

    std::string js;
    char buf[640];
    js += "(function(){var comp=app.project.itemByID(" + std::to_string(compId) + ");if(!(comp&&comp instanceof CompItem))return 'nocomp';";
    js += "var L=comp.layer(" + std::to_string(layerIdx + 1) + ");";
    js += "var fx=null,par=L.property('ADBE Effect Parade');for(var e=1;e<=par.numProperties;e++)if(par.property(e).matchName==='BANG Gradient')fx=par.property(e);";
    js += "if(!fx)return 'nofx';";
    snprintf(buf, sizeof buf, "var shape=%d;var lock=%s;var unlock=%s;var ux=%.6f,uy=%.6f;var vec=%s;",
             shape, lockOn ? "true" : "false", (mode == 3) ? "true" : "false", ux, uy, vec ? "true" : "false"); js += buf;
    js += "app.beginUndoGroup('BANG Gradient Fit');try{";
    js += "var S=fx.property('Start'),E=fx.property('End'),SP=fx.property('Contour Span (px)');";
    js += "if(unlock){var sv=S.value,ev=E.value,pv=SP.value;S.expression='';E.expression='';SP.expression='';"
          "S.setValue(sv);E.setValue(ev);SP.setValue(pv);return 'unlocked';}";
    js += "S.expression='';E.expression='';SP.expression='';";
    js += "var pre='u=['+ux+','+uy+'];r=thisLayer.sourceRectAtTime(time,false);"
          "o=" + std::string(vec ? "[thisLayer.transform.position[0]-thisLayer.transform.anchorPoint[0],"
                                   "thisLayer.transform.position[1]-thisLayer.transform.anchorPoint[1]]" : "[0,0]") + ";"
          "c=[r.left+r.width/2+o[0],r.top+r.height/2+o[1]];"
          "h=(r.width/2)*Math.abs(u[0])+(r.height/2)*Math.abs(u[1]);"
          "k=[c[0]+(u[0]<0?-1:1)*r.width/2,c[1]+(u[1]<0?-1:1)*r.height/2];';";
    js += "if(lock){";
    js +=   "if(shape===6){SP.expression='r=thisLayer.sourceRectAtTime(time,false);Math.min(r.width,r.height)/2';}";
    js +=   "else if(shape===1){S.expression=pre+'[c[0]-u[0]*h,c[1]-u[1]*h]';E.expression=pre+'[c[0]+u[0]*h,c[1]+u[1]*h]';}";
    js +=   "else if(shape===3){S.expression=pre+'c';E.expression=pre+'[c[0]+u[0]*h,c[1]+u[1]*h]';}";
    js +=   "else{S.expression=pre+'c';E.expression=pre+'k';}";
    js += "}else{";
    js +=   "var r=L.sourceRectAtTime(comp.time,false);";
    js +=   "var ox=0,oy=0;if(vec){var tg=L.property('ADBE Transform Group');"
            "var ap=tg.property('ADBE Anchor Point').value,pp=tg.property('ADBE Position').value;"
            "ox=pp[0]-ap[0];oy=pp[1]-ap[1];}";
    js +=   "var cx=r.left+r.width/2+ox,cy=r.top+r.height/2+oy;";
    js +=   "var h=(r.width/2)*Math.abs(ux)+(r.height/2)*Math.abs(uy);";
    js +=   "var kx=cx+(ux<0?-1:1)*r.width/2,ky=cy+(uy<0?-1:1)*r.height/2;";
    js +=   "if(shape===6){SP.setValue(Math.min(r.width,r.height)/2);}";
    js +=   "else if(shape===1){S.setValue([cx-ux*h,cy-uy*h]);E.setValue([cx+ux*h,cy+uy*h]);}";
    js +=   "else if(shape===3){S.setValue([cx,cy]);E.setValue([cx+ux*h,cy+uy*h]);}";
    js +=   "else{S.setValue([cx,cy]);E.setValue([kx,ky]);}";
    js += "}}finally{app.endUndoGroup();}return 'fit';})()";

    AEGP_MemHandle resH = NULL, errH = NULL;
    suites.UtilitySuite6()->AEGP_ExecuteScript(g_plugin_id, js.c_str(), FALSE, &resH, &errH);
    if (resH) suites.MemorySuite1()->AEGP_FreeMemHandle(resH);
    if (errH) suites.MemorySuite1()->AEGP_FreeMemHandle(errH);
    return PF_Err_NONE;
}

// ── 내보내기 / 불러오기 ──────────────────
//  표준 교환 포맷은 **GIMP .ggr** 를 쓴다 — 문서화된 순수 텍스트 포맷이고
//  GIMP·Krita·Inkscape 등이 그대로 읽는다. (Photoshop .grd 는 비공개 바이너리라 제외)
//  .ggr 은 ‘구간(segment)’ 목록이므로 정지점 n 개 → 구간 n-1 개로 서로 변환한다.
//  자체 포맷이 필요하면 .json 도 읽고 쓴다(정지점 그대로).
static PF_Err ImportExport(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], bool doImport)
{
    if (!g_registered) return PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH meL = NULL;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &meL) || !meL) return PF_Err_NONE;
    AEGP_CompH compH = NULL; suites.LayerSuite9()->AEGP_GetLayerParentComp(meL, &compH);
    AEGP_ItemH itemH = NULL; A_long compId = 0;
    if (compH) { suites.CompSuite11()->AEGP_GetItemFromComp(compH, &itemH); if (itemH) suites.ItemSuite9()->AEGP_GetItemID(itemH, &compId); }
    A_long layerIdx = 0; suites.LayerSuite9()->AEGP_GetLayerIndex(meL, &layerIdx);

    if (doImport) ApplyStopVisibility(in_data, BG_NUM_STOPS);   // 숨겨진 줄에는 스크립트가 값을 못 넣는다

    std::string js;
    js += "(function(){var comp=app.project.itemByID(" + std::to_string(compId) + ");if(!(comp&&comp instanceof CompItem))return 'nocomp';";
    js += "var L=comp.layer(" + std::to_string(layerIdx + 1) + ");";
    js += "var fx=null,par=L.property('ADBE Effect Parade');for(var e=1;e<=par.numProperties;e++)if(par.property(e).matchName==='BANG Gradient')fx=par.property(e);";
    js += "if(!fx)return 'nofx';";
    js += "var N=" + std::to_string(BG_NUM_STOPS) + ";";
    // 정지점을 'Stop N' 그룹으로 묶었어도 **스크립트에는 평평하게** 보인다(그룹은 ECW 표시만) — 이름으로 바로 찾으면 된다
    js += "function C(i){return fx.property('Color '+i);}function P(i){return fx.property('Position '+i);}function O(i){return fx.property('Opacity '+i);}";
    if (!doImport) {
        js += "var f=File.saveDialog('Export gradient','CSS:*.css,GIMP gradient:*.ggr,JSON:*.json');if(!f)return 'cancel';";
        js += "var sp=fx.property('Start').value,ep=fx.property('End').value;"
              "var ang=Math.round(((Math.atan2(ep[0]-sp[0],-(ep[1]-sp[1]))*180/Math.PI)%360+360)%360);"
              "var iv=fx.property('Interpolate').value;"
              "var spaceName=(iv===3?' in oklab':((iv===4||iv===5)?' in oklch':''));";
        js += "var n=fx.property('Stops').value;var st=[];";
        js += "for(var i=1;i<=n;i++){var c=C(i).value;st.push({p:P(i).value/100,r:c[0],g:c[1],b:c[2],a:O(i).value/100});}";
        js += "st.sort(function(a,b){return a.p-b.p;});";
        js += "f.open('w');f.encoding='UTF-8';";
        js += "if(/\\.css$/i.test(f.name)){var o=[];"
              "for(var i=0;i<st.length;i++){o.push('rgba('+Math.round(st[i].r*255)+','+Math.round(st[i].g*255)+','+Math.round(st[i].b*255)+','+st[i].a.toFixed(3)+') '+(st[i].p*100).toFixed(1)+'%');}"
              "f.writeln('/* BANG Gradient */');"
              "f.writeln('background: linear-gradient('+ang+'deg'+spaceName+', '+o.join(', ')+');');}"
              "else ";
        js += R"(if(/\.json$/i.test(f.name)){var o=['{"format":"BANG Gradient","version":1,"stops":['];)";
        js += R"(for(var i=0;i<st.length;i++){o.push((i?',':'')+'{"pos":'+(st[i].p*100).toFixed(4)+',"color":['+st[i].r.toFixed(6)+','+st[i].g.toFixed(6)+','+st[i].b.toFixed(6)+'],"opacity":'+(st[i].a*100).toFixed(4)+'}');})";
        js +=   "o.push(']}');f.write(o.join(''));}";
        js += "else{f.writeln('GIMP Gradient');f.writeln('Name: '+L.name);f.writeln(String(st.length-1));";
        js +=   "for(var i=0;i<st.length-1;i++){var A=st[i],B=st[i+1];";
        js +=   "f.writeln([A.p.toFixed(6),((A.p+B.p)/2).toFixed(6),B.p.toFixed(6),A.r.toFixed(6),A.g.toFixed(6),A.b.toFixed(6),A.a.toFixed(6),B.r.toFixed(6),B.g.toFixed(6),B.b.toFixed(6),B.a.toFixed(6),'0','0','0','0'].join(' '));}}";
        js += "f.close();return 'exported '+f.fsName;";
    } else {
        js += "var f=File.openDialog('Import gradient (.css / .ggr / .json)');if(!f)return 'cancel';";
        js += "f.open('r');f.encoding='UTF-8';var txt=f.read();f.close();var st=[];";
        js += "var mSp=/linear-gradient\\s*\\(\\s*in\\s+(oklab|oklch|srgb)/i.exec(txt);";
        js += "if(/linear-gradient/i.test(txt)){var re=/(#[0-9a-fA-F]{3,8}|rgba?\\(([^)]*)\\))\\s*([0-9.]+)%/g,m;"
              "while((m=re.exec(txt))!==null){var col=m[1],pp=parseFloat(m[3])/100,r,g,b,a=1;"
              "if(col.charAt(0)==='#'){var hx=col.substring(1);"
              "if(hx.length===3)hx=hx.charAt(0)+hx.charAt(0)+hx.charAt(1)+hx.charAt(1)+hx.charAt(2)+hx.charAt(2);"
              "r=parseInt(hx.substr(0,2),16)/255;g=parseInt(hx.substr(2,2),16)/255;b=parseInt(hx.substr(4,2),16)/255;"
              "if(hx.length>=8)a=parseInt(hx.substr(6,2),16)/255;}"
              "else{var pr=m[2].split(',');r=parseFloat(pr[0])/255;g=parseFloat(pr[1])/255;b=parseFloat(pr[2])/255;if(pr.length>3)a=parseFloat(pr[3]);}"
              "st.push({p:pp,r:r,g:g,b:b,a:a});}"
              "if(mSp&&mSp[1]){var sn=mSp[1].toLowerCase();"
              "fx.property('Interpolate').setValue(sn==='oklab'?3:(sn==='oklch'?4:1));}}";
        js += "else ";
        js += "if(/^\\s*\\{/.test(txt)){var o=eval('('+txt+')');if(o&&o.stops)for(var i=0;i<o.stops.length;i++){var q=o.stops[i];st.push({p:q.pos/100,r:q.color[0],g:q.color[1],b:q.color[2],a:(q.opacity===undefined?100:q.opacity)/100});}}";
        js += "else{var ln=txt.split(/\\r\\n|\\r|\\n/);if(ln[0].indexOf('GIMP Gradient')<0)return 'not a gradient file';";
        js +=   "var k=1;while(k<ln.length&&ln[k].indexOf('Name:')===0)k++;var cnt=parseInt(ln[k++],10);";
        js +=   "for(var i=0;i<cnt&&k<ln.length;i++,k++){var v=ln[k].replace(/^\\s+|\\s+$/g,'').split(/\\s+/);if(v.length<11)continue;";
        js +=   "var seg={l:parseFloat(v[0]),rr:parseFloat(v[2]),lr:parseFloat(v[3]),lg:parseFloat(v[4]),lb:parseFloat(v[5]),la:parseFloat(v[6]),"
                "rr2:parseFloat(v[7]),rg:parseFloat(v[8]),rb:parseFloat(v[9]),ra:parseFloat(v[10])};";
        js +=   "st.push({p:seg.l,r:seg.lr,g:seg.lg,b:seg.lb,a:seg.la});";
        js +=   "if(i===cnt-1)st.push({p:seg.rr,r:seg.rr2,g:seg.rg,b:seg.rb,a:seg.ra});}}";
        js += "if(st.length<2)return 'no stops';";
        // 8개를 넘으면 균등 간격으로 솎아낸다
        js += "if(st.length>N){var out=[];for(var i=0;i<N;i++)out.push(st[Math.round(i*(st.length-1)/(N-1))]);st=out;}";
        js += "app.beginUndoGroup('BANG Gradient Import');try{";
        js += "fx.property('Stops').setValue(st.length);";
        js += "for(var i=0;i<st.length;i++){C(i+1).setValue([st[i].r,st[i].g,st[i].b]);P(i+1).setValue(st[i].p*100);O(i+1).setValue(st[i].a*100);}";
        js += "}finally{app.endUndoGroup();}return 'imported '+st.length;";
    }
    js += "})()";

    AEGP_MemHandle resH = NULL, errH = NULL;
    suites.UtilitySuite6()->AEGP_ExecuteScript(g_plugin_id, js.c_str(), FALSE, &resH, &errH);
    if (resH) suites.MemorySuite1()->AEGP_FreeMemHandle(resH);
    if (errH) suites.MemorySuite1()->AEGP_FreeMemHandle(errH);
    return PF_Err_NONE;
}

// 색공간 변환은 아래에 정의되어 있다
static void OklabToLinear(float L, float a, float bb, float& r, float& g, float& b);
static float LinearToSrgb(float c);

// ── 무작위 그라데이션 ─────────────────────────
//  '눈치내기’ 없이 탁한 색(어두운 노랑·주황 = 갈색, 회끜 도는 중간색)을 만들지 않도록 두 가지를 지킨다.
//   ① **채널 클리핑 금지** — OKLCh 값을 sRGB 로 바꿀 때 범위를 넘으면 예전처럼 채널을 잘라버렸는데,
//      그러면 색상이 틀어지고 채도가 빠져 바로 탁해진다. 이제는 그 밝기에서 sRGB 안에 들어오는
//      최대 채도를 먼저 구해 그것의 비율로만 고른다(gamut-relative saturation).
//   ② **색상마다 쓸 수 있는 밝기가 다르다** — 노랑의 cusp(가장 진해지는 밝기)는 L≈0.87,
//      파랑은 L≈0.45. 노랑을 L 0.4 에 놓으면 그게 바로 갈색이다. 그래서 밝기를 cusp 주변으로 제한한다.
//  (Wijffelaars 의 cusp 삼각형 + gamut-relative saturation — meodai/cusphanger 와 같은 접근)
//  색상 배치는 유사색·넓은 스윙·보색·분할보색·삼색·단색조 여섯 가지에서 고른다.
static bool BG_InGamut(float L, float a, float b)
{
    float r, g, bl;
    OklabToLinear(L, a, b, r, g, bl);
    const float e = 1e-4f;
    return r >= -e && r <= 1.f + e && g >= -e && g <= 1.f + e && bl >= -e && bl <= 1.f + e;
}

// 그 밝기·색상에서 sRGB 안에 들어오는 최대 채도
static float BG_MaxChroma(float L, float h)
{
    const float ch = std::cos(h), sh = std::sin(h);
    float lo = 0.f, hi = 0.45f;
    for (int k = 0; k < 18; k++) {
        const float m = (lo + hi) * 0.5f;
        if (BG_InGamut(L, m * ch, m * sh)) lo = m; else hi = m;
    }
    return lo;
}

// 그 색상이 가장 진해지는 밝기(cusp)
static float BG_CuspL(float h)
{
    float bestL = 0.6f, bestC = 0.f;
    for (int k = 1; k < 40; k++) {
        const float L = k / 40.f;
        const float c = BG_MaxChroma(L, h);
        if (c > bestC) { bestC = c; bestL = L; }
    }
    return bestL;
}

static PF_Err Randomize(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    static uint32_t seed = 0x9E3779B9u;
    seed ^= (uint32_t)in_data->current_time * 2654435761u + 0x85EBCA77u;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFFFFu) / 16777215.f; };
    const float TAU = 6.2831853f, DEG = TAU / 360.f;

    const int n = 2 + (int)(rnd() * 3.99f);                  // 2~5 개
    const float h0 = rnd() * TAU;                            // 기준 색상
    const float dir = (rnd() < 0.5f) ? -1.f : 1.f;

    const float pick = rnd();
    int scheme;
    if      (pick < 0.22f) scheme = 0;   // 유사색
    else if (pick < 0.42f) scheme = 1;   // 넓은 스윙
    else if (pick < 0.60f) scheme = 2;   // 보색
    else if (pick < 0.75f) scheme = 3;   // 분할보색
    else if (pick < 0.88f) scheme = 4;   // 삼색
    else                   scheme = 5;   // 단색조
    const float step  = (15.f + rnd() * 30.f) * DEG;
    const float sweep = (120.f + rnd() * 180.f) * DEG;

    float La = 0.30f + rnd() * 0.30f, Lb = 0.62f + rnd() * 0.33f;
    if (rnd() < 0.5f) { const float t = La; La = Lb; Lb = t; }            // 밝은→어두운 방향도
    const float sa = 0.58f + rnd() * 0.42f, sb = 0.58f + rnd() * 0.42f;   // 최대 채도 대비 비율
    int neutral = 0;                                                      // 끝 정지점을 흰색/검정 쪽으로
    if (rnd() < 0.18f) neutral = (rnd() < 0.5f) ? 1 : 2;

    BG_PresetStop st[BG_NUM_STOPS];
    for (int i = 0; i < n; i++) {
        const float u = (n > 1) ? (float)i / (n - 1) : 0.f;
        float h;
        if      (scheme == 0) h = h0 + dir * step * i;
        else if (scheme == 1) h = h0 + dir * sweep * u;
        else if (scheme == 2) h = h0 + ((i & 1) ? 180.f * DEG : 0.f) + dir * 10.f * DEG * i;
        else if (scheme == 3) { const int m = i % 3; h = h0 + ((m == 0) ? 0.f : (m == 1) ? 165.f * DEG : 195.f * DEG); }
        else if (scheme == 4) h = h0 + dir * 120.f * DEG * (i % 3);
        else                  h = h0 + dir * 8.f * DEG * i;

        const float Lc = BG_CuspL(h);
        float Lmin = std::max(0.12f, Lc - 0.34f);
        // '똥색' 은 결국 **어두운 주황~노랑**(갈색·청동색·올리브)이다. OKLCh 로 h≈88° 를 중심으로 한
        //  그 띠에서만 밝기 바닥을 크게 올린다 — 나머지 색상(짙은 남색·버건디·포레스트그린)은 그대로 둔다.
        float hd = std::fmod(h * 180.f / 3.14159265f, 360.f);
        if (hd < 0.f) hd += 360.f;
        float dh = std::fabs(hd - 88.f);
        if (dh > 180.f) dh = 360.f - dh;
        const float k = (dh >= 85.f) ? 0.f : 0.5f * (1.f + std::cos(3.14159265f * dh / 85.f));
        if (k > 0.15f) Lmin = std::max(Lmin, 0.30f + 0.56f * k);
        const float Lmax = std::min(0.97f, Lc + 0.42f);
        float L = std::min(std::max(La + (Lb - La) * u, Lmin), Lmax);
        float C = (sa + (sb - sa) * u) * BG_MaxChroma(L, h);
        if (neutral && i == n - 1) { L = (neutral == 1) ? 0.96f : 0.12f; C = 0.012f; }

        float lr, lg, lb;
        OklabToLinear(L, C * std::cos(h), C * std::sin(h), lr, lg, lb);
        st[i].pos = u * 100.f;
        st[i].r = (A_u_char)(std::min(1.f, std::max(0.f, LinearToSrgb(std::min(1.f, std::max(0.f, lr))))) * 255.f + 0.5f);
        st[i].g = (A_u_char)(std::min(1.f, std::max(0.f, LinearToSrgb(std::min(1.f, std::max(0.f, lg))))) * 255.f + 0.5f);
        st[i].b = (A_u_char)(std::min(1.f, std::max(0.f, LinearToSrgb(std::min(1.f, std::max(0.f, lb))))) * 255.f + 0.5f);
        st[i].op = 100.f;
    }
    WriteStops(params, st, n);
    g_barEdit = true;
    out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    return PF_Err_NONE;
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], const PF_UserChangedParamExtra* extra)
{
    if (extra->param_index == BG_FIT_H) return ApplyFit(in_data, out_data, params, 0);
    if (extra->param_index == BG_FIT_V) return ApplyFit(in_data, out_data, params, 1);
    if (extra->param_index == BG_FIT_D) return ApplyFit(in_data, out_data, params, 4);
    if (extra->param_index == BG_LOCK)  return ApplyFit(in_data, out_data, params, params[BG_LOCK]->u.bd.value ? 2 : 3);
    if (extra->param_index == BG_RANDOM) return Randomize(in_data, out_data, params);
    if (extra->param_index == BG_IMPORT) return ImportExport(in_data, out_data, params, true);
    if (extra->param_index == BG_EXPORT) return ImportExport(in_data, out_data, params, false);
    if (extra->param_index == BG_PRESET) {
        const int k = (int)params[BG_PRESET]->u.pd.value - 1;
        if (k > 0 && k < kPresetCount && kPresets[k].n > 0) {
            WriteStops(params, kPresets[k].s, kPresets[k].n);
            out_data->out_flags |= PF_OutFlag_REFRESH_UI;
            g_barEdit = true;          // 개수 변경에 따른 위치 재분배를 막는다
        }
        return PF_Err_NONE;
    }
    if (extra->param_index == BG_COUNT && g_barEdit) { g_barEdit = false; }
    else if (extra->param_index == BG_COUNT) {
        // 정지점 개수를 바꾸면 위치를 고르게 다시 뿌려준다 — 안 그러면 그라데이션이
        // 기본값 범위에서 잘려 보이고, 사용자가 매번 손으로 숫자를 넣어야 한다.
        const int n = std::min(BG_NUM_STOPS, std::max(2, (int)params[BG_COUNT]->u.sd.value));
        for (int i = 0; i < n; i++) {
            PF_ParamDef* pd = params[BG_SP(i)];
            pd->u.fs_d.value = i * 100.0 / (n - 1);
            pd->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        }
    }
    if (extra->param_index == BG_SHAPE || extra->param_index == BG_COUNT) {
        PF_Err err = UpdateParamsUI(in_data, out_data, params);
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
        return err;
    }
    return PF_Err_NONE;
}

// ── 색공간 ───────────────────────────────────────────────────

static inline float SrgbToLinear(float c) { return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
static inline float LinearToSrgb(float c) { return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; }

// OKLab (Björn Ottosson) — 선형 sRGB 기준
static void LinearToOklab(float r, float g, float b, float& L, float& a, float& bb)
{
    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    const float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    L  = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    a  = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    bb = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
}
static void OklabToLinear(float L, float a, float bb, float& r, float& g, float& b)
{
    const float l_ = L + 0.3963377774f * a + 0.2158037573f * bb;
    const float m_ = L - 0.1055613458f * a - 0.0638541728f * bb;
    const float s_ = L - 0.0894841775f * a - 1.2914855480f * bb;
    const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    r =  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

struct RGB { float r, g, b; };

// 두 색을 고른 색공간에서 섞는다 (입력·출력 모두 sRGB 0~1)
static RGB MixColor(A_long mode, const RGB& A, const RGB& B, float u)
{
    RGB out;
    if (mode == BG_INTERP_SRGB) {
        out.r = A.r + (B.r - A.r) * u; out.g = A.g + (B.g - A.g) * u; out.b = A.b + (B.b - A.b) * u;
        return out;
    }
    const float ar = SrgbToLinear(A.r), ag = SrgbToLinear(A.g), ab = SrgbToLinear(A.b);
    const float br = SrgbToLinear(B.r), bg = SrgbToLinear(B.g), bb = SrgbToLinear(B.b);
    float lr, lg, lb;
    if (mode == BG_INTERP_LINEAR) {
        lr = ar + (br - ar) * u; lg = ag + (bg - ag) * u; lb = ab + (bb - ab) * u;
    } else {
        float L1, a1, b1, L2, a2, b2;
        LinearToOklab(ar, ag, ab, L1, a1, b1);
        LinearToOklab(br, bg, bb, L2, a2, b2);
        float L, a, b;
        if (mode == BG_INTERP_OKLAB) {
            L = L1 + (L2 - L1) * u; a = a1 + (a2 - a1) * u; b = b1 + (b2 - b1) * u;
        } else {
            // 극좌표(채도·색상환)에서 섞으면 무지개처럼 색이 돌아간다
            const float C1 = std::sqrt(a1 * a1 + b1 * b1), C2 = std::sqrt(a2 * a2 + b2 * b2);
            float h1 = std::atan2(b1, a1), h2 = std::atan2(b2, a2);
            const float TAU = 6.28318530718f;
            float dh = h2 - h1;
            while (dh >  3.14159265f) dh -= TAU;
            while (dh < -3.14159265f) dh += TAU;
            if (mode == BG_INTERP_OKLCH_LONG && std::fabs(dh) > 1e-5f) dh += (dh > 0 ? -TAU : TAU);
            const float Lh = L1 + (L2 - L1) * u, Ch = C1 + (C2 - C1) * u, hh = h1 + dh * u;
            L = Lh; a = Ch * std::cos(hh); b = Ch * std::sin(hh);
        }
        OklabToLinear(L, a, b, lr, lg, lb);
    }
    out.r = LinearToSrgb(std::min(1.f, std::max(0.f, lr)));
    out.g = LinearToSrgb(std::min(1.f, std::max(0.f, lg)));
    out.b = LinearToSrgb(std::min(1.f, std::max(0.f, lb)));
    return out;
}

// ── 커스텀 UI: 가로 색 띄 ──────────────────
//  ECW 는 파라미터를 한 줄에 하나씩밖에 못 놓는다. 그래서 정지점을 가로로 보여주려면
//  Drawbot 으로 직접 그리는 수밖에 없다 — 위에 그라데이션 미리보기, 아래에 정지점 칩.
//  칩을 누르면 AE 색 선택기가 뜨고, 고른 색이 그 정지점으로 들어간다.
struct BarChip { float x, y, w, h; int stop; };

// 오른쪽 끝 24px 는 ⇄ (좌우 반전) 버튼 자리
static const float kInvW = 22.f;
static void BarInvRect(const PF_Rect& fr, float& x, float& y, float& w, float& h)
{
    w = kInvW; h = (float)kBarStrip - 2.f;
    x = (float)fr.right - 2.f - w; y = (float)fr.top + 2.f;
}

static int BarChips(const PF_ParamDef* const* params, const PF_Rect& fr, BarChip* out, int maxN)
{
    const int count = std::min(BG_NUM_STOPS, std::max(2, (int)params[BG_COUNT]->u.sd.value));
    const float x0 = (float)fr.left + 2.f, x1 = (float)fr.right - 4.f - kInvW;
    const float w = std::max(8.f, x1 - x0 - kBarChip);
    int n = 0;
    for (int i = 0; i < count && n < maxN; i++) {
        const float pos = (float)(params[BG_SP(i)]->u.fs_d.value / 100.0);
        BarChip c;
        c.w = c.h = (float)kBarChip;
        c.x = x0 + std::min(1.f, std::max(0.f, pos)) * w;
        c.y = (float)fr.top + kBarStrip + 4.f;
        c.stop = i;
        out[n++] = c;
    }
    return n;
}

// params[] 에서 바로 t 위치의 색과 불투명도를 뽑는다 (렌더와 같은 규칙)
static RGB BarColorAt(const PF_ParamDef* const* params, float t, float* outOp)
{
    const int n = std::min(BG_NUM_STOPS, std::max(2, (int)params[BG_COUNT]->u.sd.value));
    const A_long interp = params[BG_INTERP]->u.pd.value;
    const float smooth = (float)(params[BG_SMOOTH]->u.fs_d.value / 100.0);
    float pos[BG_NUM_STOPS], opa[BG_NUM_STOPS]; RGB col[BG_NUM_STOPS];
    int idx[BG_NUM_STOPS];
    for (int i = 0; i < n; i++) {
        pos[i] = (float)(params[BG_SP(i)]->u.fs_d.value / 100.0);
        opa[i] = (float)(params[BG_SO(i)]->u.fs_d.value / 100.0);
        const PF_Pixel& c = params[BG_SC(i)]->u.cd.value;
        col[i] = { c.red / 255.f, c.green / 255.f, c.blue / 255.f };
        idx[i] = i;
    }
    for (int a = 0; a < n - 1; a++) for (int b = a + 1; b < n; b++)
        if (pos[idx[b]] < pos[idx[a]]) { const int tmp = idx[a]; idx[a] = idx[b]; idx[b] = tmp; }
    if (t <= pos[idx[0]])     { if (outOp) *outOp = opa[idx[0]];     return col[idx[0]]; }
    if (t >= pos[idx[n - 1]]) { if (outOp) *outOp = opa[idx[n - 1]]; return col[idx[n - 1]]; }
    int k = 0;
    while (k < n - 2 && t > pos[idx[k + 1]]) k++;
    const float p0 = pos[idx[k]], p1 = pos[idx[k + 1]];
    float u = (p1 - p0 > 1e-6f) ? (t - p0) / (p1 - p0) : 0.f;
    if (smooth > 0.f) u = u + (u * u * (3.f - 2.f * u) - u) * smooth;
    if (outOp) *outOp = opa[idx[k]] + (opa[idx[k + 1]] - opa[idx[k]]) * u;
    return MixColor(interp, col[idx[k]], col[idx[k + 1]], u);
}

static PF_Err BarDraw(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_EventExtra* ev)
{
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    if (ev->effect_win.area != PF_EA_CONTROL) return err;
    DRAWBOT_Suites db;
    ERR(AEFX_AcquireDrawbotSuites(in_data, out_data, &db));
    if (err) return err;
    DRAWBOT_DrawRef drawRef = NULL; DRAWBOT_SupplierRef sup = NULL; DRAWBOT_SurfaceRef surf = NULL;
    PF_EffectCustomUISuite1* cui = NULL;
    ERR(AEFX_AcquireSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, NULL, (void**)&cui));
    if (!err && cui) { ERR(cui->PF_GetDrawingReference(ev->contextH, &drawRef)); AEFX_ReleaseSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, NULL); }
    ERR(db.drawbot_suiteP->GetSupplier(drawRef, &sup));
    ERR(db.drawbot_suiteP->GetSurface(drawRef, &surf));
    if (!err) {
        db.surface_suiteP->PushStateStack(surf);
        const PF_Rect& fr = ev->effect_win.current_frame;
        const float x0 = (float)fr.left + 2.f, x1 = (float)fr.right - 4.f - kInvW;
        const float stripY = (float)fr.top + 2.f, stripW = std::max(4.f, x1 - x0);
        const bool rev = params[BG_REVERSE]->u.bd.value != 0;
        // 1) 미리보기 띄 — 투명도가 보이도록 체크무늬 위에 알파대로 얘은다
        {
            const float cell = 6.f;
            for (float y2 = 0; y2 < (float)kBarStrip - 2.f && !err; y2 += cell) {
                for (float x2 = 0; x2 < stripW && !err; x2 += cell) {
                    const bool odd = (((int)(x2 / cell) + (int)(y2 / cell)) & 1) != 0;
                    const float g0 = odd ? 0.38f : 0.55f;
                    const DRAWBOT_ColorRGBA cg = { g0, g0, g0, 1.f };
                    DRAWBOT_BrushRef bg2 = NULL; ERR(db.supplier_suiteP->NewBrush(sup, &cg, &bg2));
                    DRAWBOT_PathRef pg = NULL; ERR(db.supplier_suiteP->NewPath(sup, &pg));
                    DRAWBOT_RectF32 rg = { x0 + x2, stripY + y2,
                                           std::min(cell, stripW - x2), std::min(cell, (float)kBarStrip - 2.f - y2) };
                    ERR(db.path_suiteP->AddRect(pg, &rg));
                    ERR(db.surface_suiteP->FillPath(surf, bg2, pg, kDRAWBOT_FillType_Default));
                    if (pg)  ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pg));
                    if (bg2) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bg2));
                }
            }
        }
        for (float x = 0; x < stripW && !err; x += 2.f) {
            float t = (stripW > 1.f) ? x / (stripW - 1.f) : 0.f;
            if (rev) t = 1.f - t;
            float op = 1.f;
            const RGB c = BarColorAt(params, t, &op);
            const DRAWBOT_ColorRGBA cc = { c.r, c.g, c.b, std::min(1.f, std::max(0.f, op)) };
            DRAWBOT_BrushRef br = NULL; ERR(db.supplier_suiteP->NewBrush(sup, &cc, &br));
            DRAWBOT_PathRef path = NULL; ERR(db.supplier_suiteP->NewPath(sup, &path));
            DRAWBOT_RectF32 rr = { x0 + x, stripY, 2.f, (float)kBarStrip - 2.f };
            ERR(db.path_suiteP->AddRect(path, &rr));
            ERR(db.surface_suiteP->FillPath(surf, br, path, kDRAWBOT_FillType_Default));
            if (path) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path));
            if (br)   ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)br));
        }
        // 2) 정지점 칩
        const DRAWBOT_ColorRGBA cEdge = { 0.10f, 0.10f, 0.10f, 1.f }, cRim = { 0.85f, 0.85f, 0.85f, 1.f };
        DRAWBOT_PenRef pen = NULL, rim = NULL;
        ERR(db.supplier_suiteP->NewPen(sup, &cEdge, 1.f, &pen));
        ERR(db.supplier_suiteP->NewPen(sup, &cRim, 1.f, &rim));
        BarChip chips[BG_NUM_STOPS];
        const int n = BarChips(params, fr, chips, BG_NUM_STOPS);
        for (int i = 0; i < n && !err; i++) {
            const PF_Pixel& pc = params[BG_SC(chips[i].stop)]->u.cd.value;
            const float cop = (float)(params[BG_SO(chips[i].stop)]->u.fs_d.value / 100.0);
            // 칩 뒤에도 체크무늬 — 불투명도 0 에 가까울수록 투명하게 보인다
            for (int q = 0; q < 4 && !err; q++) {
                const float hw = chips[i].w * 0.5f, hh = chips[i].h * 0.5f;
                const float g0 = ((q & 1) ^ (q >> 1)) ? 0.38f : 0.55f;
                const DRAWBOT_ColorRGBA cg = { g0, g0, g0, 1.f };
                DRAWBOT_BrushRef bg2 = NULL; ERR(db.supplier_suiteP->NewBrush(sup, &cg, &bg2));
                DRAWBOT_PathRef pg = NULL; ERR(db.supplier_suiteP->NewPath(sup, &pg));
                DRAWBOT_RectF32 rg = { chips[i].x + 0.5f + (q & 1) * hw, chips[i].y + 0.5f + (q >> 1) * hh, hw, hh };
                ERR(db.path_suiteP->AddRect(pg, &rg));
                ERR(db.surface_suiteP->FillPath(surf, bg2, pg, kDRAWBOT_FillType_Default));
                if (pg)  ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pg));
                if (bg2) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bg2));
            }
            const DRAWBOT_ColorRGBA cc = { pc.red / 255.f, pc.green / 255.f, pc.blue / 255.f, std::min(1.f, std::max(0.f, cop)) };
            DRAWBOT_BrushRef br = NULL; ERR(db.supplier_suiteP->NewBrush(sup, &cc, &br));
            DRAWBOT_PathRef path = NULL; ERR(db.supplier_suiteP->NewPath(sup, &path));
            DRAWBOT_RectF32 rr = { chips[i].x + 0.5f, chips[i].y + 0.5f, chips[i].w, chips[i].h };
            ERR(db.path_suiteP->AddRect(path, &rr));
            ERR(db.surface_suiteP->FillPath(surf, br, path, kDRAWBOT_FillType_Default));
            ERR(db.surface_suiteP->StrokePath(surf, rim, path));
            if (path) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path));
            if (br)   ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)br));
        }
        // 3) ⇄ 좌우 반전 버튼
        {
            float bx, by, bw, bh; BarInvRect(fr, bx, by, bw, bh);
            const DRAWBOT_ColorRGBA cBtn = { 0.28f, 0.28f, 0.28f, 1.f }, cTxt = { 0.92f, 0.92f, 0.92f, 1.f };
            DRAWBOT_BrushRef bb = NULL, bt = NULL; DRAWBOT_FontRef font = NULL;
            float fontSize = 11.f; db.supplier_suiteP->GetDefaultFontSize(sup, &fontSize);
            ERR(db.supplier_suiteP->NewBrush(sup, &cBtn, &bb));
            ERR(db.supplier_suiteP->NewBrush(sup, &cTxt, &bt));
            ERR(db.supplier_suiteP->NewDefaultFont(sup, fontSize, &font));
            DRAWBOT_PathRef path = NULL; ERR(db.supplier_suiteP->NewPath(sup, &path));
            DRAWBOT_RectF32 rr = { bx + 0.5f, by + 0.5f, bw, bh };
            ERR(db.path_suiteP->AddRect(path, &rr));
            ERR(db.surface_suiteP->FillPath(surf, bb, path, kDRAWBOT_FillType_Default));
            ERR(db.surface_suiteP->StrokePath(surf, rim, path));
            DRAWBOT_UTF16Char txt[4] = { 0x21C4, 0 };
            DRAWBOT_PointF32 org = { bx + bw * 0.5f, by + bh * 0.5f + fontSize * 0.36f };
            ERR(db.surface_suiteP->DrawString(surf, bt, font, txt, &org, kDRAWBOT_TextAlignment_Center, kDRAWBOT_TextTruncation_None, 0.f));
            if (path) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path));
            if (font) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)font));
            if (bt)   ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bt));
            if (bb)   ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bb));
        }
        if (rim) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)rim));
        if (pen) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pen));
        db.surface_suiteP->PopStateStack(surf);
    }
    ERR2(AEFX_ReleaseDrawbotSuites(in_data, out_data));
    if (!err) ev->evt_out_flags = PF_EO_HANDLED_EVENT;
    return err;
}

static PF_Err BarClick(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_EventExtra* ev)
{
    PF_Err err = PF_Err_NONE;
    if (ev->effect_win.area != PF_EA_CONTROL) return err;
    const PF_Point pt = ev->u.do_click.screen_point;
    const PF_Rect& fr = ev->effect_win.current_frame;
    AEGP_SuiteHandler suites0(in_data->pica_basicP);

    // ⇄ : 정지점 위치를 좌우로 뒤집는다 (Reverse 체크박스와 달리 값 자체를 바꿔 계속 편집할 수 있다)
    {
        float bx, by, bw, bh; BarInvRect(fr, bx, by, bw, bh);
        if (pt.h >= bx && pt.h < bx + bw && pt.v >= by && pt.v < by + bh) {
            const int cnt = std::min(BG_NUM_STOPS, std::max(2, (int)params[BG_COUNT]->u.sd.value));
            for (int i = 0; i < cnt; i++) {
                PF_ParamDef* pp = params[BG_SP(i)];
                pp->u.fs_d.value = 100.0 - pp->u.fs_d.value;
                pp->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            }
            PF_Rect inval(fr);
            suites0.AppSuite4()->PF_InvalidateRect(ev->contextH, &inval);
            ev->evt_out_flags = PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
            return err;
        }
    }

    BarChip chips[BG_NUM_STOPS];
    const int n = BarChips(params, fr, chips, BG_NUM_STOPS);

    // 띄를 더블클릭하면 그 자리에 정지점을 넣는다 (포토샵·AE 그라데이션 편집기처럼)
    if (ev->u.do_click.num_clicks >= 2) {
        const float x0 = (float)fr.left + 2.f, x1 = (float)fr.right - 4.f - kInvW;
        const float stripY = (float)fr.top + 2.f;
        const bool onStrip = (pt.v >= stripY && pt.v < stripY + kBarStrip && pt.h >= x0 && pt.h < x1);
        const int cnt = std::min(BG_NUM_STOPS, std::max(2, (int)params[BG_COUNT]->u.sd.value));
        if (onStrip && cnt < BG_NUM_STOPS) {
            float t = (x1 - x0 > 1.f) ? ((float)pt.h - x0) / (x1 - x0 - 1.f) : 0.f;
            t = std::min(1.f, std::max(0.f, t));
            float op0 = 1.f;
            const RGB c = BarColorAt(params, params[BG_REVERSE]->u.bd.value ? 1.f - t : t, &op0);
            const int i = cnt;                      // 맨 뒤에 붙여도 렌더·띄가 위치순으로 정렬한다
            params[BG_SC(i)]->u.cd.value.red   = (A_u_char)(std::min(1.f, std::max(0.f, c.r)) * 255.f + 0.5f);
            params[BG_SC(i)]->u.cd.value.green = (A_u_char)(std::min(1.f, std::max(0.f, c.g)) * 255.f + 0.5f);
            params[BG_SC(i)]->u.cd.value.blue  = (A_u_char)(std::min(1.f, std::max(0.f, c.b)) * 255.f + 0.5f);
            params[BG_SC(i)]->uu.change_flags  = PF_ChangeFlag_CHANGED_VALUE;
            params[BG_SP(i)]->u.fs_d.value = t * 100.0;
            params[BG_SP(i)]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            params[BG_SO(i)]->u.fs_d.value = op0 * 100.0;
            params[BG_SO(i)]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            params[BG_COUNT]->u.sd.value = cnt + 1;
            params[BG_COUNT]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            g_barEdit = true;                       // 위치 재분배로 덮어쓰지 않게
            PF_Rect inval(fr);
            suites0.AppSuite4()->PF_InvalidateRect(ev->contextH, &inval);
            ev->evt_out_flags = PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
            return err;
        }
    }

    for (int i = 0; i < n; i++) {
        const BarChip& c = chips[i];
        if (pt.h < c.x || pt.h >= c.x + c.w || pt.v < c.y || pt.v >= c.y + c.h) continue;
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        const int idx = BG_SC(c.stop);
        const PF_Pixel& cur = params[idx]->u.cd.value;
        PF_PixelFloat in = { 1.f, cur.red / 255.f, cur.green / 255.f, cur.blue / 255.f }, outc = in;
        char title[64]; snprintf(title, sizeof title, "Color %d", c.stop + 1);
        if (!suites.AppSuite6()->PF_AppColorPickerDialog(title, &in, TRUE, &outc)) {
            params[idx]->u.cd.value.red   = (A_u_char)(std::min(1.f, std::max(0.f, outc.red))   * 255.f + 0.5f);
            params[idx]->u.cd.value.green = (A_u_char)(std::min(1.f, std::max(0.f, outc.green)) * 255.f + 0.5f);
            params[idx]->u.cd.value.blue  = (A_u_char)(std::min(1.f, std::max(0.f, outc.blue))  * 255.f + 0.5f);
            params[idx]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        }
        PF_Rect inval(fr);
        suites.AppSuite4()->PF_InvalidateRect(ev->contextH, &inval);
        ev->evt_out_flags = PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
        break;
    }
    return err;
}

static PF_Err HandleEvent(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, PF_EventExtra* ev)
{
    if (!ev || ev->effect_win.index != BG_BAR) return PF_Err_NONE;
    switch (ev->e_type) {
        case PF_Event_DRAW:     return BarDraw(in_data, out_data, params, ev);
        case PF_Event_DO_CLICK: return BarClick(in_data, out_data, params, ev);
        default: break;
    }
    return PF_Err_NONE;
}

// ── 거리 변환 (Contour 용, BANG Stroke 와 동일) ────────────────
static void edt1d(const float* f, float* dOut, int n, std::vector<int>& v, std::vector<float>& z)
{
    const float INF = 1e20f;
    v.resize(n); z.resize(n + 1);
    int k = 0; v[0] = 0; z[0] = -INF; z[1] = INF;
    for (int q = 1; q < n; q++) {
        float s;
        while (true) {
            const int vk = v[k];
            s = ((f[q] + q * (float)q) - (f[vk] + vk * (float)vk)) / (2.0f * (q - vk));
            if (s <= z[k]) { k--; if (k < 0) { k = 0; break; } } else break;
        }
        if (k == 0 && s <= z[0]) { v[0] = q; z[0] = -INF; z[1] = INF; continue; }
        k++; v[k] = q; z[k] = s; z[k + 1] = INF;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        const int vk = v[k];
        dOut[q] = (q - vk) * (float)(q - vk) + f[vk];
    }
}
static void edt2d(std::vector<float>& g, int w, int h)
{
    std::vector<float> col(std::max(w, h)), out(std::max(w, h));
    std::vector<int> v; std::vector<float> z;
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) col[y] = g[(size_t)y * w + x];
        edt1d(col.data(), out.data(), h, v, z);
        for (int y = 0; y < h; y++) g[(size_t)y * w + x] = out[y];
    }
    for (int y = 0; y < h; y++) {
        edt1d(&g[(size_t)y * w], out.data(), w, v, z);
        memcpy(&g[(size_t)y * w], out.data(), (size_t)w * sizeof(float));
    }
}

// ── 프리렌더 ─────────────────────────────────────────────────

static void DeletePreRenderData(void* p) { delete reinterpret_cast<BG_PreRenderData*>(p); }

static PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef pd;
    BG_PreRenderData* d = new BG_PreRenderData();
    memset(d, 0, sizeof(*d));

    PF_FpLong dsx = (PF_FpLong)in_data->downsample_x.num / (PF_FpLong)in_data->downsample_x.den;
    PF_FpLong dsy = (PF_FpLong)in_data->downsample_y.num / (PF_FpLong)in_data->downsample_y.den;
    if (dsx <= 0) dsx = 1;
    if (dsy <= 0) dsy = 1;

    #define CHK(idx) AEFX_CLR_STRUCT(pd); ERR(PF_CHECKOUT_PARAM(in_data, idx, in_data->current_time, in_data->time_step, in_data->time_scale, &pd));
    CHK(BG_SHAPE);          d->shape       = pd.u.pd.value;
    CHK(BG_START);          d->sx = FIX_2_FLOAT(pd.u.td.x_value); d->sy = FIX_2_FLOAT(pd.u.td.y_value);
    CHK(BG_END);            d->ex = FIX_2_FLOAT(pd.u.td.x_value); d->ey = FIX_2_FLOAT(pd.u.td.y_value);
    CHK(BG_ANGLE_OFF);      d->angleOff    = FIX_2_FLOAT(pd.u.ad.value);
    CHK(BG_CONTOUR_SPAN);   d->contourSpan = std::max(1.0, pd.u.fs_d.value * std::min(dsx, dsy));
    CHK(BG_REPEAT);         d->repeatMode  = pd.u.pd.value;
    CHK(BG_CYCLES);         d->cycles      = std::max(0.01, pd.u.fs_d.value);
    CHK(BG_PHASE);          d->phase       = pd.u.fs_d.value / 100.0;
    CHK(BG_REVERSE);        d->reverse     = pd.u.bd.value != 0;
    CHK(BG_INTERP);         d->interp      = pd.u.pd.value;
    CHK(BG_SMOOTH);         d->smooth      = pd.u.fs_d.value / 100.0;
    CHK(BG_COUNT);          d->count       = std::min(BG_NUM_STOPS, std::max(2, (int)pd.u.sd.value));
    CHK(BG_ALPHA_MODE);     d->alphaMode   = pd.u.pd.value;
    CHK(BG_DITHER);         d->dither      = pd.u.fs_d.value / 100.0;
    CHK(BG_BLEND);          d->blend       = pd.u.pd.value;
    CHK(BG_AMOUNT);         d->amount      = pd.u.fs_d.value / 100.0;
    CHK(BG_PRESERVE_ALPHA); d->preserveAlpha = pd.u.bd.value != 0;
    for (int i = 0; i < BG_NUM_STOPS; i++) {
        CHK(BG_SC(i));
        d->stops[i].r = pd.u.cd.value.red / 255.0; d->stops[i].g = pd.u.cd.value.green / 255.0; d->stops[i].b = pd.u.cd.value.blue / 255.0;
        CHK(BG_SP(i));   d->stops[i].pos     = pd.u.fs_d.value / 100.0;
        CHK(BG_SO(i));   d->stops[i].opacity = pd.u.fs_d.value / 100.0;
    }
    #undef CHK
    if (err) { delete d; return err; }

    // 정지점을 위치 순으로 (사용자가 뒤섞어 놔도 되도록)
    std::sort(d->stops, d->stops + d->count, [](const BG_Stop& a, const BG_Stop& b) { return a.pos < b.pos; });

    PF_CheckoutResult in_result;
    ERR(extra->cb->checkout_layer(in_data->effect_ref, BG_INPUT, BG_INPUT, &extra->input->output_request,
                                  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (!err) {
        d->in_rect = in_result.result_rect;
        extra->output->max_result_rect = in_result.max_result_rect;
        extra->output->result_rect     = in_result.result_rect;
        d->out_rect = in_result.result_rect;
        extra->output->solid = FALSE;
        extra->output->pre_render_data = d;
        extra->output->delete_pre_render_data_func = DeletePreRenderData;
    } else {
        delete d;
    }
    return err;
}

// ── 픽셀 타입별 접근 ─────────────────────────────────────────
template <typename P> struct Chan;
template <> struct Chan<PF_Pixel>      { static float get(A_u_char v)  { return v / 255.0f; }   static A_u_char  put(float f) { return (A_u_char)(std::min(std::max(f, 0.f), 1.f) * 255.0f + 0.5f); } };
template <> struct Chan<PF_Pixel16>    { static float get(A_u_short v) { return v / 32768.0f; } static A_u_short put(float f) { return (A_u_short)(std::min(std::max(f, 0.f), 1.f) * 32768.0f + 0.5f); } };
template <> struct Chan<PF_PixelFloat> { static float get(float v)     { return v; }            static float     put(float f) { return f; } };

static inline float Hash01(int x, int y)
{
    uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 16777215.f;
}

static inline void BlendRGB(A_long mode, float br_, float bg_, float bb_, float& r, float& g, float& b)
{
    auto ov = [](float base, float top) { return base < 0.5f ? 2.f * base * top : 1.f - 2.f * (1.f - base) * (1.f - top); };
    switch (mode) {
        case BG_BLEND_MULTIPLY: r *= br_; g *= bg_; b *= bb_; break;
        case BG_BLEND_SCREEN:   r = 1 - (1 - r) * (1 - br_); g = 1 - (1 - g) * (1 - bg_); b = 1 - (1 - b) * (1 - bb_); break;
        case BG_BLEND_ADD:      r = std::min(1.f, r + br_); g = std::min(1.f, g + bg_); b = std::min(1.f, b + bb_); break;
        case BG_BLEND_OVERLAY:  r = ov(br_, r); g = ov(bg_, g); b = ov(bb_, b); break;
        default: break;   // Normal
    }
}

// 정지점 목록에서 t 위치의 색과 불투명도를 구한다
static RGB StopColorAt(const BG_PreRenderData* d, float t, float& outAlpha)
{
    const BG_Stop* S = d->stops;
    const int n = (int)d->count;
    const float smooth = (float)d->smooth;
    if (t <= S[0].pos)        { outAlpha = (float)S[0].opacity;     return { (float)S[0].r, (float)S[0].g, (float)S[0].b }; }
    if (t >= S[n - 1].pos)    { outAlpha = (float)S[n - 1].opacity; return { (float)S[n - 1].r, (float)S[n - 1].g, (float)S[n - 1].b }; }
    int k = 0;
    while (k < n - 2 && t > S[k + 1].pos) k++;
    const float p0 = (float)S[k].pos, p1 = (float)S[k + 1].pos;
    float u = (p1 - p0 > 1e-6f) ? (t - p0) / (p1 - p0) : 0.f;
    if (smooth > 0.f) u = u + (u * u * (3.f - 2.f * u) - u) * smooth;   // 선형 ↔ smoothstep
    outAlpha = (float)(S[k].opacity + (S[k + 1].opacity - S[k].opacity) * u);
    const RGB A = { (float)S[k].r, (float)S[k].g, (float)S[k].b };
    const RGB B = { (float)S[k + 1].r, (float)S[k + 1].g, (float)S[k + 1].b };
    return MixColor(d->interp, A, B, u);
}

template <typename P>
static PF_Err RenderGradient(const BG_PreRenderData* d, const PF_EffectWorld* in, PF_EffectWorld* out)
{
    const int ow = out->width, oh = out->height;
    const int iw = in ? in->width : 0, ih = in ? in->height : 0;
    const int offx = d->out_rect.left - d->in_rect.left, offy = d->out_rect.top - d->in_rect.top;

    const float sx = (float)d->sx, sy = (float)d->sy, ex = (float)d->ex, ey = (float)d->ey;
    float dx = ex - sx, dy = ey - sy;
    float len2 = dx * dx + dy * dy;
    if (len2 < 1e-6f) { dx = 1; dy = 0; len2 = 1; }
    const float invLen2 = 1.f / len2;
    const float len = std::sqrt(len2);
    const float angOff = (float)(d->angleOff * 3.14159265358979 / 180.0);
    const float cycles = (float)d->cycles, phase = (float)d->phase;
    const float smooth = (float)d->smooth, dither = (float)d->dither;
    const float amount = (float)d->amount;

    // Contour: 알파 경계까지의 거리장을 한 번 만든다
    std::vector<float> contour;
    if (d->shape == BG_SHAPE_CONTOUR && in) {
        const int cw = iw, ch = ih;
        contour.assign((size_t)cw * ch, 0.f);
        std::vector<float> inside((size_t)cw * ch);
        const float INF = 1e20f;
        for (int y = 0; y < ch; y++) {
            const P* row = (const P*)((const char*)in->data + (size_t)y * in->rowbytes);
            for (int x = 0; x < cw; x++) inside[(size_t)y * cw + x] = (Chan<P>::get(row[x].alpha) >= 0.5f) ? INF : 0.f;
        }
        edt2d(inside, cw, ch);
        for (size_t i = 0; i < inside.size(); i++) contour[i] = std::sqrt(inside[i]);
    }
    const float span = (float)d->contourSpan;

    // 모양 → t (반복·위상·방향까지 적용, 디더는 제외). 픽셀 안 어느 지점이든 불러 쓸 수 있게 람다로.
    auto shapeT = [&](float px, float py, int ix, int iy) -> float {
        float t;
        switch (d->shape) {
            case BG_SHAPE_RADIAL:  t = std::sqrt((px - sx) * (px - sx) + (py - sy) * (py - sy)) / len; break;
            case BG_SHAPE_ANGULAR: {
                float a = std::atan2(py - sy, px - sx) - std::atan2(dy, dx) - angOff;
                const float TAU = 6.28318530718f;
                a = a - std::floor(a / TAU) * TAU;
                t = a / TAU;
                break;
            }
            case BG_SHAPE_DIAMOND: {
                const float u = ((px - sx) * dx + (py - sy) * dy) / len2;
                const float v = ((px - sx) * -dy + (py - sy) * dx) / len2;
                t = std::fabs(u) + std::fabs(v);
                break;
            }
            case BG_SHAPE_REFLECT: t = std::fabs(((px - sx) * dx + (py - sy) * dy) * invLen2); break;
            case BG_SHAPE_CONTOUR: {
                const float dist = (!contour.empty() && ix >= 0 && ix < iw && iy >= 0 && iy < ih)
                                 ? contour[(size_t)iy * iw + ix] : 0.f;
                t = dist / span;
                break;
            }
            default:               t = ((px - sx) * dx + (py - sy) * dy) * invLen2; break;   // Linear
        }
        t = t * cycles + phase;
        switch (d->repeatMode) {
            case BG_REPEAT_REPEAT: t = t - std::floor(t); break;
            case BG_REPEAT_MIRROR: { const float f = std::fabs(t); const float m = std::fmod(f, 2.f); t = (m > 1.f) ? 2.f - m : m; break; }
            default: t = std::min(1.f, std::max(0.f, t)); break;
        }
        return d->reverse ? 1.f - t : t;
    };

    // 반복·Angular 은 t 가 1→0 으로 뚝 끊기는 이음매가 생긴다. 그 줄은 한 픽셀 안에서 색이
    // 통째로 바뀜어 계단처럼 보이므로, 이음매에 닿는 픽셀만 4×4 로 잘게 썼다.
    // 이웃 t 를 매번 다시 구하면 비싸니 세 줄을 돌려 쓴다.
    std::vector<float> tPrev(ow), tCur(ow), tNext(ow);
    auto fillRow = [&](std::vector<float>& dst, int yy) {
        const float pyy = (float)(d->out_rect.top + yy) + 0.5f;
        for (int x = 0; x < ow; x++) dst[x] = shapeT((float)(d->out_rect.left + x) + 0.5f, pyy, x + offx, yy + offy);
    };
    fillRow(tCur, 0);
    fillRow(tPrev, -1);
    const float SEAM = 0.45f;

    for (int y = 0; y < oh; y++) {
        P* orow = (P*)((char*)out->data + (size_t)y * out->rowbytes);
        const int iy = y + offy;
        const P* irow = (in && iy >= 0 && iy < ih) ? (const P*)((const char*)in->data + (size_t)iy * in->rowbytes) : nullptr;
        const float py = (float)(d->out_rect.top + y) + 0.5f;
        if (y + 1 < oh) fillRow(tNext, y + 1); else fillRow(tNext, y + 1);
        for (int x = 0; x < ow; x++) {
            const int ix = x + offx;
            float ba = 0, brr = 0, bgg = 0, bbb = 0;            // 원본 (straight)
            if (irow && ix >= 0 && ix < iw) {
                const P& p = irow[ix];
                ba = Chan<P>::get(p.alpha); brr = Chan<P>::get(p.red); bgg = Chan<P>::get(p.green); bbb = Chan<P>::get(p.blue);
            }
            const float px = (float)(d->out_rect.left + x) + 0.5f;

            float t = tCur[x];
            if (dither > 0.f) t += (Hash01(x + (int)d->out_rect.left, y + (int)d->out_rect.top) - 0.5f) * dither * (1.f / 255.f);

            // 이음매 검사: 옆·위·아래 t 와 0.45 넘게 벌어지면 한 픽셀 안에서 색이 뚝 끊긴다는 뜻
            bool seam = false;
            if (x > 0        && std::fabs(tCur[x] - tCur[x - 1]) > SEAM) seam = true;
            if (x + 1 < ow   && std::fabs(tCur[x] - tCur[x + 1]) > SEAM) seam = true;
            if (!seam        && std::fabs(tCur[x] - tPrev[x])    > SEAM) seam = true;
            if (!seam        && std::fabs(tCur[x] - tNext[x])    > SEAM) seam = true;

            RGB col; float ga;
            if (seam) {
                // 4×4 서브샘플을 premultiplied 로 평균 (알파가 다를 수 있으므로)
                float ar = 0, ag2 = 0, ab = 0, aa = 0;
                for (int sy2 = 0; sy2 < 4; sy2++) for (int sx2 = 0; sx2 < 4; sx2++) {
                    const float ssx = px - 0.5f + (sx2 + 0.5f) * 0.25f;
                    const float ssy = py - 0.5f + (sy2 + 0.5f) * 0.25f;
                    float a2; const RGB c2 = StopColorAt(d, shapeT(ssx, ssy, ix, iy), a2);
                    ar += c2.r * a2; ag2 += c2.g * a2; ab += c2.b * a2; aa += a2;
                }
                const float inv16 = 1.f / 16.f;
                aa *= inv16;
                col = (aa > 1e-6f) ? RGB{ ar * inv16 / aa, ag2 * inv16 / aa, ab * inv16 / aa } : RGB{ 0, 0, 0 };
                ga = aa;
            } else {
                col = StopColorAt(d, t, ga);
            }
            ga *= amount;
            if (d->preserveAlpha && d->alphaMode != BG_ALPHA_REPLACE) ga *= ba;

            // 4) 합성
            float gr = col.r, gg = col.g, gb = col.b;
            if (d->blend != BG_BLEND_NORMAL && ba > 0.f) BlendRGB(d->blend, brr, bgg, bbb, gr, gg, gb);
            float oa, orr, og, ob;
            if (d->alphaMode == BG_ALPHA_REPLACE) {
                // 그라데이션 불투명도가 곰 알파가 된다 — 0% 인 자리는 원본이 비치는 게 아니라 구멍이 끩다
                oa = (d->preserveAlpha ? ba : 1.f) * ga;
                orr = gr * oa; og = gg * oa; ob = gb * oa;
            } else {
                oa = ba; orr = brr * ba; og = bgg * ba; ob = bbb * ba;
                const float k = 1.f - ga;
                oa = ga + oa * k; orr = gr * ga + orr * k; og = gg * ga + og * k; ob = gb * ga + ob * k;
            }

            P& o = orow[x];
            const float inv = (oa > 1e-6f) ? 1.f / oa : 0.f;
            o.alpha = Chan<P>::put(oa); o.red = Chan<P>::put(orr * inv); o.green = Chan<P>::put(og * inv); o.blue = Chan<P>::put(ob * inv);
        }
        tPrev.swap(tCur);
        tCur.swap(tNext);
    }
    return PF_Err_NONE;
}

static PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    const BG_PreRenderData* d = reinterpret_cast<const BG_PreRenderData*>(extra->input->pre_render_data);
    if (!d) return PF_Err_INTERNAL_STRUCT_DAMAGED;

    PF_EffectWorld* inW = nullptr; PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, BG_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));
    if (err || !outW) return err;

    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
    PF_PixelFormat fmt = PF_PixelFormat_INVALID;
    ERR(ws->PF_GetPixelFormat(outW, &fmt));
    if (err) return err;

    switch (fmt) {
        case PF_PixelFormat_ARGB128: err = RenderGradient<PF_PixelFloat>(d, inW, outW); break;
        case PF_PixelFormat_ARGB64:  err = RenderGradient<PF_Pixel16>(d, inW, outW);    break;
        default:                     err = RenderGradient<PF_Pixel>(d, inW, outW);      break;
    }
    return err;
}

// ── 등록 / 진입점 ───────────────────────────────────────────

extern "C" DllExport PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr, SPBasicSuite* inSPBasicSuitePtr,
    const char* inHostName, const char* inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr,
        "BANG Gradient",                     // Name
        "BANG Gradient",                     // Match Name
        "BANG",                              // Category
        AE_RESERVED_INFO,
        "EffectMain",
        "https://github.com/galaon/BANG_Toolbox");
    return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:              err = About(in_data, out_data, params, output); break;
            case PF_Cmd_GLOBAL_SETUP:       err = GlobalSetup(in_data, out_data, params, output); break;
            case PF_Cmd_PARAMS_SETUP:       err = ParamsSetup(in_data, out_data, params, output); break;
            case PF_Cmd_UPDATE_PARAMS_UI:   err = UpdateParamsUI(in_data, out_data, params); break;
            case PF_Cmd_USER_CHANGED_PARAM: err = UserChangedParam(in_data, out_data, params, (const PF_UserChangedParamExtra*)extra); break;
            case PF_Cmd_EVENT:              err = HandleEvent(in_data, out_data, params, output, (PF_EventExtra*)extra); break;
            case PF_Cmd_SMART_PRE_RENDER:   err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
            case PF_Cmd_SMART_RENDER:       err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
            default: break;
        }
    } catch (PF_Err& thrown) { err = thrown; }
    catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; }
    return err;
}
