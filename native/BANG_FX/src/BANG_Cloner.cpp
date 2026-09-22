// BANG_Cloner.cpp — BANG Cloner
//  소스 레이어에 적용하는 인스턴스 클로너. 현재 프레임의 입력 이미지를 N개의 변환(이동·회전·크기)으로 합성한다.
//  · 레이어 복제가 없으므로 개수와 무관하게 소스 애니메이션 타이밍이 정확히 유지된다 (Motion Tile 모델)
//  · 출력 버퍼를 모든 클론의 경계 합집합으로 확장 (PF_OutFlag_I_EXPAND_BUFFER)
//  · 배치: Linear / Grid / Radial / Path. 간격은 "이웃 클론 경계 사이 px(Gap)" — 소스 크기와 무관, 음수 = 겹침
//  · Path: 컴프 안 셰이프 레이어(펜 패스·사각형·타원, 그룹 변환·레이어 변환 반영 — AEGP 스트림 순회)를 따라 길이 기준 균등 배치.
//    Path Layer 가 없으면 이 레이어의 마스크 패스(PF_PathQuerySuite). Start/End 범위, Offset, Speed(자동 흐름), Reverse, Loop, Align.
//  · 원본 위치 기준: Linear 는 Origin Index, Grid 는 Origin X/Y, Radial 은 Center on Object / Center
//  · 단계 변환(회전·크기·불투명도) + 랜덤(위치·회전·크기·불투명도, 시드). 그룹(Linear/Grid/Radial/Path/Step/Random):
//    배치 모드에 맞는 그룹만 펼치고 나머지는 접음(PF_UpdateParamUI), Grid 에선 Count 숨김 (AEGP DynamicStream HIDDEN)
//  · 퀵 버튼(커스텀 ECW UI, Drawbot): Start Angle·Rotation Step·Sweep 증감 Preset(가운데 = 리셋), Grid Origin 3×3
//  · Bake to Layers 버튼: 현재 시간의 클론 변환을 ExtendScript 로 넘겨 실제 레이어(복제본)로 굳힘
//  · 8 / 16 / 32bpc, SmartFX. (PreRender 에서 AEGP 를 쓰므로 MFR 플래그는 켜지 않음 — 렌더 내부는 행 단위 멀티스레드)
//  · 렌더: 입력을 premultiplied float 로 한 번 변환 → 클론마다 (정수 이동이면 직접 복사, 아니면 증분 바이리니어) over 합성.

#include "BANG_Cloner.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <functional>

#ifdef AE_OS_WIN
#include <windows.h>
#endif

static AEGP_PluginID g_plugin_id = 0;
static bool          g_registered = false;

static const double PI = 3.14159265358979323846;

// ── 명령 처리 ────────────────────────────────────────────────

static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "BANG Cloner v%d.%d\rInstance cloner (Linear / Grid / Radial / Path) - BANG_Toolbox",
        BANG_CLONER_MAJOR, BANG_CLONER_MINOR);
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(BANG_CLONER_MAJOR, BANG_CLONER_MINOR, BANG_CLONER_BUG, BANG_CLONER_STAGE, BANG_CLONER_BUILD);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER | PF_OutFlag_SEND_UPDATE_PARAMS_UI | PF_OutFlag_CUSTOM_UI |
                           PF_OutFlag_NON_PARAM_VARY;   // Path Speed 는 시간에 따라 변함
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_REVEALS_ZERO_ALPHA |
                           PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;   // 그룹 flags 존중 (Linear 만 펼침)
    if (!g_registered && in_data->appl_id != kAppID_Premiere) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        if (suites.UtilitySuite3()->AEGP_RegisterWithAEGP(NULL, "BANG Cloner", &g_plugin_id) == A_Err_NONE) g_registered = true;
    }
    return PF_Err_NONE;
}

// ── 퀵 버튼 정의 (커스텀 ECW UI) ──
// kind: 2 = Origin 3×3 (대상 X/Y 두 개) · 3 = 각도 증감 · 4 = float 증감 (값 0 인 버튼은 resetTo 로 리셋)
struct QuickRow { int param; int kind; int target; int target2; int n; const float* values; const char* const* labels; int cols; float resetTo; };
static const float  kAngleDelta[]  = { -360, -90, -15, -2.5f, -1, 0, 1, 2.5f, 15, 90, 360 };
static const char* const kAngleDeltaLbls[] = { "-360", "-90", "-15", "-2.5", "-1", "0", "+1", "+2.5", "+15", "+90", "+360" };
static const char* const kSweepDeltaLbls[] = { "-360", "-90", "-15", "-2.5", "-1", "360", "+1", "+2.5", "+15", "+90", "+360" };   // 가운데 = 360 으로 리셋
static const float  kOriginVals[]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };   // 3×3 인덱스: 열 = i%3, 행 = i/3 (0 first · 1 center · 2 last)
static const char* const kOriginLbls[] = { "", "", "", "", "", "", "", "", "" };
static const QuickRow kQuick[] = {
    { BC_ORIGIN_QUICK, 2, BC_ORIGIN_X,    BC_ORIGIN_Y, 9,  kOriginVals, kOriginLbls,     3,  0 },
    { BC_START_QUICK,  3, BC_START_ANGLE, -1,          11, kAngleDelta, kAngleDeltaLbls, 11, 0 },
    { BC_SWEEP_QUICK,  4, BC_SWEEP,       -1,          11, kAngleDelta, kSweepDeltaLbls, 11, 360 },
    { BC_ROT_QUICK,    3, BC_ROT_STEP,    -1,          11, kAngleDelta, kAngleDeltaLbls, 11, 0 },
};
static const QuickRow* FindQuick(int param) { for (const QuickRow& q : kQuick) if (q.param == param) return &q; return nullptr; }
static const int kQuickBtnH = 22, kQuickBtnGap = 3, kQuickBtnW = 22;   // 정사각 버튼

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    #define LINE "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"   // ──────── (이름 31바이트 제한 → 8칸)
    #define QUICK_ROW(NAME, ID, ROWS) do { AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_CANNOT_TIME_VARY; def.ui_flags = PF_PUI_CONTROL; def.ui_width = 300; def.ui_height = kQuickBtnH * (ROWS) + kQuickBtnGap * ((ROWS) - 1) + 2; PF_ADD_CHECKBOX(NAME, "", FALSE, 0, ID); } while (0)
    #define TOPIC(NAME, ID)        do { AEFX_CLR_STRUCT(def); PF_ADD_TOPIC(NAME, ID); } while (0)
    #define TOPIC_CLOSED(NAME, ID) do { AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_START_COLLAPSED; PF_ADD_TOPIC(NAME, ID); } while (0)
    #define TOPIC_END(ID)          do { AEFX_CLR_STRUCT(def); PF_END_TOPIC(ID); } while (0)
    #define FSLIDER(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, ID) do { AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, 0, ID); } while (0)

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Layout", 4, BC_MODE_LINEAR, "Linear|Grid|Radial|Path", PF_ParamFlag_SUPERVISE, BC_MODE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Count", 1, 1000, 1, 50, 5, BC_COUNT);

    TOPIC("Linear " LINE, BC_G_LINEAR);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Origin Index", 1, 1000, 1, 50, 1, BC_ORIGIN);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Direction", 2, BC_DIR_H, "Horizontal|Vertical", BC_DIR);
    FSLIDER("Gap (px)", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, BC_GAP);
    FSLIDER("Offset (px)", -10000, 10000, -500, 500, 0, PF_Precision_TENTHS, 0, BC_OFFSET);
    TOPIC_END(BC_G_LINEAR_END);

    TOPIC_CLOSED("Grid " LINE, BC_G_GRID);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Columns", 1, 100, 1, 20, 3, BC_COLS);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Rows", 1, 100, 1, 20, 3, BC_ROWS);
    FSLIDER("Gap X (px)", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, BC_GAP_X);
    FSLIDER("Gap Y (px)", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, BC_GAP_Y);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Origin X", 1, 100, 1, 20, 2, BC_ORIGIN_X);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Origin Y", 1, 100, 1, 20, 2, BC_ORIGIN_Y);
    QUICK_ROW("Origin Preset", BC_ORIGIN_QUICK, 3);
    TOPIC_END(BC_G_GRID_END);

    TOPIC_CLOSED("Radial " LINE, BC_G_RADIAL);
    FSLIDER("Radius (px)", -10000, 10000, 0, 1000, 200, PF_Precision_TENTHS, 0, BC_RADIUS);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;   // 다이얼 접힘
    PF_ADD_ANGLE("Start Angle", 0, BC_START_ANGLE);
    QUICK_ROW("Start Angle Preset", BC_START_QUICK, 1);
    FSLIDER("Sweep (deg)", -3600, 3600, 0, 360, 360, PF_Precision_TENTHS, 0, BC_SWEEP);
    QUICK_ROW("Sweep Preset", BC_SWEEP_QUICK, 1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Face Outward", FALSE, 0, BC_FACE_OUT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Center on Object", TRUE, PF_ParamFlag_SUPERVISE, BC_CENTER_OBJ);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Center", 50, 50, 0, BC_CENTER);
    TOPIC_END(BC_G_RADIAL_END);

    TOPIC_CLOSED("Path " LINE, BC_G_PATH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Path Layer", PF_LayerDefault_NONE, BC_PATH_LAYER);
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_PATH;
    PF_STRCPY(def.PF_DEF_NAME, "Mask Path (no layer)");
    def.u.path_d.dephault = 0;
    def.uu.id = BC_PATH;
    ERR(PF_ADD_PARAM(in_data, -1, &def));
    FSLIDER("Start", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BC_PATH_START);
    FSLIDER("End", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BC_PATH_END);
    FSLIDER("Offset", -10000, 10000, -100, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BC_PATH_OFFSET);
    FSLIDER("Speed (%/s)", -10000, 10000, -100, 100, 0, PF_Precision_TENTHS, 0, BC_PATH_SPEED);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Reverse", FALSE, 0, BC_PATH_REVERSE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Loop", TRUE, 0, BC_PATH_LOOP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Align to Path", TRUE, 0, BC_PATH_ALIGN);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    PF_ADD_ANGLE("Align Angle", 0, BC_PATH_ANGLE);
    TOPIC_END(BC_G_PATH_END);

    TOPIC_CLOSED("Step " LINE, BC_G_STEP);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    PF_ADD_ANGLE("Rotation Step", 0, BC_ROT_STEP);
    QUICK_ROW("Rotation Step Preset", BC_ROT_QUICK, 1);
    FSLIDER("Scale Step", -1000, 1000, -50, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BC_SCALE_STEP);
    FSLIDER("End Opacity", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BC_OPACITY_END);
    TOPIC_END(BC_G_STEP_END);

    TOPIC_CLOSED("Random " LINE, BC_G_RANDOM);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Seed", 0, 9999, 0, 100, 0, BC_SEED);
    FSLIDER("Random Position (px)", 0, 10000, 0, 500, 0, PF_Precision_TENTHS, 0, BC_RAND_POS);
    FSLIDER("Random Rotation (deg)", 0, 180, 0, 180, 0, PF_Precision_TENTHS, 0, BC_RAND_ROT);
    FSLIDER("Random Scale", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BC_RAND_SCALE);
    FSLIDER("Random Opacity", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BC_RAND_OPACITY);
    TOPIC_END(BC_G_RANDOM_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Bake", "Bake to Layers", 0, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, BC_BAKE);
    #undef FSLIDER
    #undef QUICK_ROW
    #undef TOPIC
    #undef TOPIC_CLOSED
    #undef TOPIC_END
    #undef LINE

    // 커스텀 UI(ECW 이벤트) 등록
    PF_CustomUIInfo ci;
    AEFX_CLR_STRUCT(ci);
    ci.events = PF_CustomEFlag_EFFECT;
    ci.comp_ui_alignment = ci.layer_ui_alignment = ci.preview_ui_alignment = PF_UIAlignment_NONE;
    err = (*(in_data->inter.register_ui))(in_data->effect_ref, &ci);

    out_data->num_params = BC_NUM_PARAMS;
    return err;
}

// ── 파라미터 값 묶음 (프리렌더 = 체크아웃, Bake = params[]) ──

struct BC_Params {
    A_long mode = BC_MODE_LINEAR, count = 5, origin = 1, dir = BC_DIR_H, cols = 3, rows = 3, originX = 2, originY = 2;
    A_long faceOut = 0, seed = 0, centerObj = 1, pathId = 0, pathLayerId = 0, pathReverse = 0, pathLoop = 1, pathAlign = 1;
    double gap = 20, offset = 0, gapX = 20, gapY = 20, radius = 200, startAng = 0, sweep = 360, cx = 0, cy = 0;
    double pathStart = 0, pathEnd = 1, pathOffset = 0, pathSpeed = 0, pathAngle = 0;
    double rotStep = 0, scaleStep = 0, opEnd = 1, randPos = 0, randRot = 0, randScale = 0, randOpacity = 0;
};

// 파라미터 하나를 읽는 두 가지 방법을 같은 코드로 처리
struct ParamSource {
    PF_InData* in_data; PF_ParamDef** params;   // params 가 있으면 그대로, 없으면 체크아웃
    PF_Err err = PF_Err_NONE;
    template <typename F> void get(int idx, F fn) {
        if (params) { fn(*params[idx]); return; }
        PF_ParamDef pd; AEFX_CLR_STRUCT(pd);
        PF_Err e = PF_CHECKOUT_PARAM(in_data, idx, in_data->current_time, in_data->time_step, in_data->time_scale, &pd);
        if (e) { err = e; return; }
        fn(pd);
        PF_CHECKIN_PARAM(in_data, &pd);
    }
};

static PF_Err ReadParams(PF_InData* in_data, PF_ParamDef* params[], BC_Params& p)
{
    ParamSource src{ in_data, params };
    src.get(BC_MODE,         [&](PF_ParamDef& d) { p.mode = d.u.pd.value; });
    src.get(BC_COUNT,        [&](PF_ParamDef& d) { p.count = d.u.sd.value; });
    src.get(BC_ORIGIN,       [&](PF_ParamDef& d) { p.origin = d.u.sd.value; });
    src.get(BC_DIR,          [&](PF_ParamDef& d) { p.dir = d.u.pd.value; });
    src.get(BC_GAP,          [&](PF_ParamDef& d) { p.gap = d.u.fs_d.value; });
    src.get(BC_OFFSET,       [&](PF_ParamDef& d) { p.offset = d.u.fs_d.value; });
    src.get(BC_COLS,         [&](PF_ParamDef& d) { p.cols = d.u.sd.value; });
    src.get(BC_ROWS,         [&](PF_ParamDef& d) { p.rows = d.u.sd.value; });
    src.get(BC_GAP_X,        [&](PF_ParamDef& d) { p.gapX = d.u.fs_d.value; });
    src.get(BC_GAP_Y,        [&](PF_ParamDef& d) { p.gapY = d.u.fs_d.value; });
    src.get(BC_ORIGIN_X,     [&](PF_ParamDef& d) { p.originX = d.u.sd.value; });
    src.get(BC_ORIGIN_Y,     [&](PF_ParamDef& d) { p.originY = d.u.sd.value; });
    src.get(BC_RADIUS,       [&](PF_ParamDef& d) { p.radius = d.u.fs_d.value; });
    src.get(BC_START_ANGLE,  [&](PF_ParamDef& d) { p.startAng = FIX_2_FLOAT(d.u.ad.value); });
    src.get(BC_SWEEP,        [&](PF_ParamDef& d) { p.sweep = d.u.fs_d.value; });
    src.get(BC_FACE_OUT,     [&](PF_ParamDef& d) { p.faceOut = d.u.bd.value; });
    src.get(BC_CENTER_OBJ,   [&](PF_ParamDef& d) { p.centerObj = d.u.bd.value; });
    src.get(BC_CENTER,       [&](PF_ParamDef& d) { p.cx = FIX_2_FLOAT(d.u.td.x_value); p.cy = FIX_2_FLOAT(d.u.td.y_value); });
    src.get(BC_PATH,         [&](PF_ParamDef& d) { p.pathId = d.u.path_d.path_id; });
    src.get(BC_PATH_START,   [&](PF_ParamDef& d) { p.pathStart = d.u.fs_d.value / 100.0; });
    src.get(BC_PATH_END,     [&](PF_ParamDef& d) { p.pathEnd = d.u.fs_d.value / 100.0; });
    src.get(BC_PATH_OFFSET,  [&](PF_ParamDef& d) { p.pathOffset = d.u.fs_d.value / 100.0; });
    src.get(BC_PATH_SPEED,   [&](PF_ParamDef& d) { p.pathSpeed = d.u.fs_d.value / 100.0; });
    src.get(BC_PATH_REVERSE, [&](PF_ParamDef& d) { p.pathReverse = d.u.bd.value; });
    src.get(BC_PATH_LOOP,    [&](PF_ParamDef& d) { p.pathLoop = d.u.bd.value; });
    src.get(BC_PATH_ALIGN,   [&](PF_ParamDef& d) { p.pathAlign = d.u.bd.value; });
    src.get(BC_PATH_ANGLE,   [&](PF_ParamDef& d) { p.pathAngle = FIX_2_FLOAT(d.u.ad.value); });
    src.get(BC_ROT_STEP,     [&](PF_ParamDef& d) { p.rotStep = FIX_2_FLOAT(d.u.ad.value); });
    src.get(BC_SCALE_STEP,   [&](PF_ParamDef& d) { p.scaleStep = d.u.fs_d.value / 100.0; });
    src.get(BC_OPACITY_END,  [&](PF_ParamDef& d) { p.opEnd = d.u.fs_d.value / 100.0; });
    src.get(BC_SEED,         [&](PF_ParamDef& d) { p.seed = d.u.sd.value; });
    src.get(BC_RAND_POS,     [&](PF_ParamDef& d) { p.randPos = d.u.fs_d.value; });
    src.get(BC_RAND_ROT,     [&](PF_ParamDef& d) { p.randRot = d.u.fs_d.value; });
    src.get(BC_RAND_SCALE,   [&](PF_ParamDef& d) { p.randScale = d.u.fs_d.value / 100.0; });
    src.get(BC_RAND_OPACITY, [&](PF_ParamDef& d) { p.randOpacity = d.u.fs_d.value / 100.0; });
    return src.err;
}

// ── 결정적 난수: 인덱스·시드·용도별 [-1, 1] ──
static float Rnd(uint32_t i, uint32_t seed, uint32_t salt)
{
    uint32_t h = (i + 1u) * 0x9E3779B1u ^ (seed + 1u) * 0x85EBCA77u ^ (salt + 1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

// ── 패스 샘플러: 베지어 세그먼트 목록 → 길이 테이블 → 위치/접선 ──

struct PathPt { double x, y, ang; };

struct PathCurve {
    struct Seg { double x[4], y[4]; };          // 3차 베지어 (p0, c1, c2, p3)
    std::vector<Seg> segs;
    bool closed = false;
    std::vector<double> cum;                    // 세그먼트별 누적 길이 (세분화 16 조각 기준)
    double total = 0;
    void finish() {
        cum.clear(); total = 0;
        for (const Seg& s : segs) {
            double len = 0, px = s.x[0], py = s.y[0];
            for (int k = 1; k <= 16; k++) { double t = k / 16.0; double bx, by; eval(s, t, bx, by); len += std::hypot(bx - px, by - py); px = bx; py = by; }
            total += len; cum.push_back(total);
        }
    }
    static void eval(const Seg& s, double t, double& x, double& y) {
        double u = 1 - t;
        x = u*u*u*s.x[0] + 3*u*u*t*s.x[1] + 3*u*t*t*s.x[2] + t*t*t*s.x[3];
        y = u*u*u*s.y[0] + 3*u*u*t*s.y[1] + 3*u*t*t*s.y[2] + t*t*t*s.y[3];
    }
    static void deriv(const Seg& s, double t, double& dx, double& dy) {
        double u = 1 - t;
        dx = 3*u*u*(s.x[1]-s.x[0]) + 6*u*t*(s.x[2]-s.x[1]) + 3*t*t*(s.x[3]-s.x[2]);
        dy = 3*u*u*(s.y[1]-s.y[0]) + 6*u*t*(s.y[2]-s.y[1]) + 3*t*t*(s.y[3]-s.y[2]);
    }
    // 길이 위치(0..total) → 점/접선. 세그먼트 안에서는 길이 비례 근사 (16 조각 보간)
    PathPt at(double dist) const {
        PathPt r{ 0, 0, 0 };
        if (segs.empty() || total <= 0) return r;
        dist = std::min(std::max(dist, 0.0), total);
        size_t i = 0; while (i + 1 < segs.size() && dist > cum[i]) i++;
        double segStart = (i == 0) ? 0 : cum[i - 1], segLen = cum[i] - segStart;
        double want = dist - segStart;
        // 조각 단위로 길이를 누적해 t 찾기
        const Seg& s = segs[i]; double acc = 0, px = s.x[0], py = s.y[0], t = 1;
        for (int k = 1; k <= 16; k++) {
            double tk = k / 16.0, bx, by; eval(s, tk, bx, by);
            double l = std::hypot(bx - px, by - py);
            if (acc + l >= want || k == 16) { double f = (l > 0) ? std::min(1.0, std::max(0.0, (want - acc) / l)) : 0; t = (k - 1) / 16.0 + f / 16.0; break; }
            acc += l; px = bx; py = by;
        }
        (void)segLen;
        double x, y, dx, dy; eval(s, t, x, y); deriv(s, t, dx, dy);
        if (std::hypot(dx, dy) < 1e-9) { dx = s.x[3] - s.x[0]; dy = s.y[3] - s.y[0]; }
        r.x = x; r.y = y; r.ang = std::atan2(dy, dx) * 180.0 / PI;
        return r;
    }
};

// 2D 아핀 (행벡터 규약: p' = p·M — AE 의 A_Matrix4 와 같음)
struct Aff { double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    void apply(double& x, double& y) const { double nx = x * a + y * c + tx, ny = x * b + y * d + ty; x = nx; y = ny; }
    Aff inverse() const { double det = a * d - b * c; if (std::fabs(det) < 1e-12) det = 1e-12; Aff r; r.a = d / det; r.b = -b / det; r.c = -c / det; r.d = a / det; r.tx = -(tx * r.a + ty * r.c); r.ty = -(tx * r.b + ty * r.d); return r; }
};

// 셰이프 그룹 변환 체인 (안쪽 → 바깥쪽): p' = R·S·(p − anchor) + position
struct GroupXf { double ax, ay, px, py, sx, sy, rot; };
static void ApplyGroupChain(const std::vector<GroupXf>& chain, double& x, double& y)
{
    for (const GroupXf& g : chain) {
        double dx = (x - g.ax) * g.sx, dy = (y - g.ay) * g.sy;
        double r = g.rot * PI / 180.0, cs = std::cos(r), sn = std::sin(r);
        x = dx * cs - dy * sn + g.px; y = dx * sn + dy * cs + g.py;
    }
}

// 스트림 값 헬퍼
static bool StreamVal2D(AEGP_SuiteHandler& suites, AEGP_StreamRefH parent, const char* match, const A_Time& t, double& x, double& y)
{
    AEGP_StreamRefH sH = NULL; bool ok = false;
    if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(g_plugin_id, parent, match, &sH) && sH) {
        AEGP_StreamValue2 v; AEFX_CLR_STRUCT(v);
        if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(g_plugin_id, sH, AEGP_LTimeMode_CompTime, &t, FALSE, &v)) { x = v.val.two_d.x; y = v.val.two_d.y; ok = true; suites.StreamSuite5()->AEGP_DisposeStreamValue(&v); }
        suites.StreamSuite5()->AEGP_DisposeStream(sH);
    }
    return ok;
}
static bool StreamVal1D(AEGP_SuiteHandler& suites, AEGP_StreamRefH parent, const char* match, const A_Time& t, double& v1)
{
    AEGP_StreamRefH sH = NULL; bool ok = false;
    if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(g_plugin_id, parent, match, &sH) && sH) {
        AEGP_StreamValue2 v; AEFX_CLR_STRUCT(v);
        if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(g_plugin_id, sH, AEGP_LTimeMode_CompTime, &t, FALSE, &v)) { v1 = v.val.one_d; ok = true; suites.StreamSuite5()->AEGP_DisposeStreamValue(&v); }
        suites.StreamSuite5()->AEGP_DisposeStream(sH);
    }
    return ok;
}

// 셰이프 레이어의 벡터 그룹을 재귀로 돌며 패스(펜·사각형·타원)를 베지어 세그먼트로 모은다 (좌표: 셰이프 레이어 공간)
static void CollectShapePaths(AEGP_SuiteHandler& suites, AEGP_StreamRefH group, const A_Time& t, std::vector<GroupXf>& chain, std::vector<PathCurve>& out)
{
    A_long n = 0;
    if (suites.DynamicStreamSuite4()->AEGP_GetNumStreamsInGroup(group, &n)) return;
    for (A_long i = 0; i < n; i++) {
        AEGP_StreamRefH ch = NULL;
        if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByIndex(g_plugin_id, group, i, &ch) || !ch) continue;
        A_char match[AEGP_MAX_STREAM_MATCH_NAME_SIZE] = { 0 };
        suites.DynamicStreamSuite4()->AEGP_GetMatchName(ch, match);
        if (!strcmp(match, "ADBE Vector Group")) {
            GroupXf g{ 0, 0, 0, 0, 1, 1, 0 };
            AEGP_StreamRefH xf = NULL;
            if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(g_plugin_id, ch, "ADBE Vector Transform Group", &xf) && xf) {
                double sx = 100, sy = 100;
                StreamVal2D(suites, xf, "ADBE Vector Anchor", t, g.ax, g.ay);
                StreamVal2D(suites, xf, "ADBE Vector Position", t, g.px, g.py);
                StreamVal2D(suites, xf, "ADBE Vector Scale", t, sx, sy); g.sx = sx / 100.0; g.sy = sy / 100.0;
                StreamVal1D(suites, xf, "ADBE Vector Rotation", t, g.rot);
                suites.StreamSuite5()->AEGP_DisposeStream(xf);
            }
            AEGP_StreamRefH contents = NULL;
            if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(g_plugin_id, ch, "ADBE Vectors Group", &contents) && contents) {
                chain.insert(chain.begin(), g);   // 안쪽 그룹이 먼저 적용되도록 앞에 넣는다
                CollectShapePaths(suites, contents, t, chain, out);
                chain.erase(chain.begin());
                suites.StreamSuite5()->AEGP_DisposeStream(contents);
            }
        } else if (!strcmp(match, "ADBE Vector Shape - Group")) {
            AEGP_StreamRefH ps = NULL;
            if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(g_plugin_id, ch, "ADBE Vector Shape", &ps) && ps) {
                AEGP_StreamValue2 v; AEFX_CLR_STRUCT(v);
                if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(g_plugin_id, ps, AEGP_LTimeMode_CompTime, &t, FALSE, &v) && v.val.mask) {
                    A_Boolean open = FALSE; A_long nseg = 0;
                    suites.MaskOutlineSuite3()->AEGP_IsMaskOutlineOpen(v.val.mask, &open);
                    suites.MaskOutlineSuite3()->AEGP_GetMaskOutlineNumSegments(v.val.mask, &nseg);
                    PathCurve pc; pc.closed = !open;
                    for (A_long s = 0; s < nseg; s++) {
                        AEGP_MaskVertex a, b;
                        if (suites.MaskOutlineSuite3()->AEGP_GetMaskOutlineVertexInfo(v.val.mask, s, &a)) break;
                        if (suites.MaskOutlineSuite3()->AEGP_GetMaskOutlineVertexInfo(v.val.mask, s + 1, &b)) break;
                        PathCurve::Seg sg;
                        double pts[4][2] = { { a.x, a.y }, { a.x + a.tan_out_x, a.y + a.tan_out_y }, { b.x + b.tan_in_x, b.y + b.tan_in_y }, { b.x, b.y } };
                        for (int k = 0; k < 4; k++) { double x = pts[k][0], y = pts[k][1]; ApplyGroupChain(chain, x, y); sg.x[k] = x; sg.y[k] = y; }
                        pc.segs.push_back(sg);
                    }
                    if (!pc.segs.empty()) { pc.finish(); out.push_back(pc); }
                    suites.StreamSuite5()->AEGP_DisposeStreamValue(&v);
                }
                suites.StreamSuite5()->AEGP_DisposeStream(ps);
            }
        } else if (!strcmp(match, "ADBE Vector Shape - Rect") || !strcmp(match, "ADBE Vector Shape - Ellipse")) {
            const bool isRect = (match[strlen(match) - 1] == 't');
            double w = 100, h = 100, cx = 0, cy = 0, rnd = 0;
            StreamVal2D(suites, ch, isRect ? "ADBE Vector Rect Size" : "ADBE Vector Ellipse Size", t, w, h);
            StreamVal2D(suites, ch, isRect ? "ADBE Vector Rect Position" : "ADBE Vector Ellipse Position", t, cx, cy);
            if (isRect) StreamVal1D(suites, ch, "ADBE Vector Rect Roundness", t, rnd);
            PathCurve pc; pc.closed = true;
            const double hw = w / 2, hh = h / 2, K = 0.5522847498;
            auto addSeg = [&](double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3) {
                PathCurve::Seg sg; double px[4] = { x0, x1, x2, x3 }, py[4] = { y0, y1, y2, y3 };
                for (int k = 0; k < 4; k++) { double x = cx + px[k], y = cy + py[k]; ApplyGroupChain(chain, x, y); sg.x[k] = x; sg.y[k] = y; }
                pc.segs.push_back(sg);
            };
            if (!isRect) {
                addSeg(hw, 0,  hw, hh * K,  hw * K, hh,  0, hh);
                addSeg(0, hh,  -hw * K, hh,  -hw, hh * K,  -hw, 0);
                addSeg(-hw, 0,  -hw, -hh * K,  -hw * K, -hh,  0, -hh);
                addSeg(0, -hh,  hw * K, -hh,  hw, -hh * K,  hw, 0);
            } else {
                double r = std::min(rnd, std::min(hw, hh));
                if (r <= 0.01) {
                    addSeg(-hw, -hh, -hw, -hh, hw, -hh, hw, -hh); addSeg(hw, -hh, hw, -hh, hw, hh, hw, hh);
                    addSeg(hw, hh, hw, hh, -hw, hh, -hw, hh);     addSeg(-hw, hh, -hw, hh, -hw, -hh, -hw, -hh);
                } else {
                    // 상단 직선 → 우상 모서리 → 우측 → 우하 → 하단 → 좌하 → 좌측 → 좌상
                    addSeg(-hw + r, -hh, -hw + r, -hh, hw - r, -hh, hw - r, -hh);
                    addSeg(hw - r, -hh, hw - r + r * K, -hh, hw, -hh + r - r * K, hw, -hh + r);
                    addSeg(hw, -hh + r, hw, -hh + r, hw, hh - r, hw, hh - r);
                    addSeg(hw, hh - r, hw, hh - r + r * K, hw - r + r * K, hh, hw - r, hh);
                    addSeg(hw - r, hh, hw - r, hh, -hw + r, hh, -hw + r, hh);
                    addSeg(-hw + r, hh, -hw + r - r * K, hh, -hw, hh - r + r * K, -hw, hh - r);
                    addSeg(-hw, hh - r, -hw, hh - r, -hw, -hh + r, -hw, -hh + r);
                    addSeg(-hw, -hh + r, -hw, -hh + r - r * K, -hw + r - r * K, -hh, -hw + r, -hh);
                }
            }
            pc.finish(); out.push_back(pc);
        }
        suites.StreamSuite5()->AEGP_DisposeStream(ch);
    }
}

static Aff AffFromMatrix(const A_Matrix4& m) { Aff a; a.a = m.mat[0][0]; a.b = m.mat[0][1]; a.c = m.mat[1][0]; a.d = m.mat[1][1]; a.tx = m.mat[3][0]; a.ty = m.mat[3][1]; return a; }

// 셰이프 레이어(Path Layer)의 패스를 이 레이어의 버퍼 좌표(풀해상도, 좌상단 원점)로 가져온다. 성공 시 curves 채움.
static bool GatherPathLayerCurves(PF_InData* in_data, A_long layerParamIdx, std::vector<PathCurve>& curves, bool& anyClosed)
{
    if (!g_registered) return false;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH meL = NULL; if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &meL) || !meL) return false;
    AEGP_CompH compH = NULL; if (suites.LayerSuite9()->AEGP_GetLayerParentComp(meL, &compH) || !compH) return false;
    A_Time lt = { in_data->current_time, (A_u_long)in_data->time_scale }, ct = lt;
    suites.LayerSuite9()->AEGP_ConvertLayerToCompTime(meL, &lt, &ct);

    // 레이어 파라미터 → 레이어 ID → 레이어
    AEGP_EffectRefH fxH = NULL;
    if (suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(g_plugin_id, in_data->effect_ref, &fxH) || !fxH) return false;
    AEGP_LayerH pathL = NULL;
    {
        AEGP_StreamRefH sH = NULL;
        if (!suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(g_plugin_id, fxH, layerParamIdx, &sH) && sH) {
            AEGP_StreamValue2 v; AEFX_CLR_STRUCT(v);
            if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(g_plugin_id, sH, AEGP_LTimeMode_CompTime, &ct, FALSE, &v)) {
                if (v.val.layer_id) suites.LayerSuite9()->AEGP_GetLayerFromLayerID(compH, v.val.layer_id, &pathL);
                suites.StreamSuite5()->AEGP_DisposeStreamValue(&v);
            }
            suites.StreamSuite5()->AEGP_DisposeStream(sH);
        }
    }
    suites.EffectSuite2()->AEGP_DisposeEffect(fxH);
    if (!pathL) return false;
    AEGP_ObjectType ot = AEGP_ObjectType_AV; suites.LayerSuite9()->AEGP_GetLayerObjectType(pathL, &ot);
    if (ot != AEGP_ObjectType_VECTOR) return false;

    // 셰이프 콘텐츠 순회
    std::vector<PathCurve> raw; std::vector<GroupXf> chain;
    {
        AEGP_StreamRefH root = NULL, vec = NULL;
        if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefForLayer(g_plugin_id, pathL, &root) && root) {
            if (!suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(g_plugin_id, root, "ADBE Root Vectors Group", &vec) && vec) {
                CollectShapePaths(suites, vec, ct, chain, raw);
                suites.StreamSuite5()->AEGP_DisposeStream(vec);
            }
            suites.StreamSuite5()->AEGP_DisposeStream(root);
        }
    }
    if (raw.empty()) return false;

    // 셰이프 레이어 공간 → 월드 → 이 레이어 공간 → 버퍼(좌상단 원점)
    A_Matrix4 mS, mMe;
    if (suites.LayerSuite9()->AEGP_GetLayerToWorldXform(pathL, &ct, &mS)) return false;
    if (suites.LayerSuite9()->AEGP_GetLayerToWorldXform(meL, &ct, &mMe)) return false;
    // 이 레이어가 셰이프/텍스트(컴프 공간에서 래스터라이즈)면 이펙트 버퍼 좌표 == 컴프(월드) 좌표 → 역변환 없음.
    // 솔리드/푸티지는 버퍼 = 레이어 공간(좌상단 원점) → 레이어→월드 행렬의 역.
    AEGP_ObjectType meType = AEGP_ObjectType_AV; suites.LayerSuite9()->AEGP_GetLayerObjectType(meL, &meType);
    const bool meIsWorld = (meType == AEGP_ObjectType_VECTOR || meType == AEGP_ObjectType_TEXT);
    Aff toWorld = AffFromMatrix(mS), toMe = meIsWorld ? Aff() : AffFromMatrix(mMe).inverse();
    const double meHalfW = 0, meHalfH = 0;
    anyClosed = false;
    for (PathCurve& pc : raw) {
        for (PathCurve::Seg& s : pc.segs) for (int k = 0; k < 4; k++) {
            double x = s.x[k], y = s.y[k];
            toWorld.apply(x, y); toMe.apply(x, y);
            s.x[k] = x + meHalfW; s.y[k] = y + meHalfH;
        }
        pc.finish();
        if (pc.closed) anyClosed = true;
        curves.push_back(pc);
    }
    return !curves.empty();
}

// 이 레이어의 마스크 패스 (Path Layer 가 없을 때) → 버퍼 좌표
static bool GatherMaskCurve(PF_InData* in_data, A_long pathId, std::vector<PathCurve>& curves, bool& anyClosed)
{
    if (!pathId) return false;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_PathOutlinePtr pathP = NULL;
    if (suites.PathQuerySuite1()->PF_CheckoutPath(in_data->effect_ref, pathId, in_data->current_time, in_data->time_step, in_data->time_scale, &pathP) || !pathP) return false;
    PF_Boolean openB = FALSE; A_long nseg = 0;
    suites.PathDataSuite1()->PF_PathIsOpen(in_data->effect_ref, pathP, &openB);
    suites.PathDataSuite1()->PF_PathNumSegments(in_data->effect_ref, pathP, &nseg);
    // (셰이프/텍스트 레이어의 마스크 패스 좌표는 컴프 좌표로 오고, 그 레이어의 이펙트 버퍼도 컴프 좌표라 보정 불필요)
    const double ox = 0, oy = 0;
    PathCurve pc; pc.closed = !openB;
    for (A_long s = 0; s < nseg; s++) {
        PF_PathVertex a, b;
        if (suites.PathDataSuite1()->PF_PathVertexInfo(in_data->effect_ref, pathP, s, &a)) break;
        if (suites.PathDataSuite1()->PF_PathVertexInfo(in_data->effect_ref, pathP, s + 1, &b)) break;
        PathCurve::Seg sg;
        double pts[4][2] = { { a.x, a.y }, { a.x + a.tan_out_x, a.y + a.tan_out_y }, { b.x + b.tan_in_x, b.y + b.tan_in_y }, { b.x, b.y } };
        for (int k = 0; k < 4; k++) { sg.x[k] = pts[k][0] - ox; sg.y[k] = pts[k][1] - oy; }
        pc.segs.push_back(sg);
    }
    suites.PathQuerySuite1()->PF_CheckinPath(in_data->effect_ref, pathId, FALSE, pathP);
    if (pc.segs.empty()) return false;
    pc.finish(); anyClosed = pc.closed; curves.push_back(pc);
    return true;
}

// 패스 위 클론 위치 목록 (버퍼 좌표, 풀해상도 → ds 는 호출측에서)
static void SamplePath(const std::vector<PathCurve>& curves, bool anyClosed, const BC_Params& p, double timeSec, A_long n, std::vector<PathPt>& out)
{
    double total = 0; for (const PathCurve& c : curves) total += c.total;
    if (total <= 0 || n < 1) return;
    const bool singleClosed = (curves.size() == 1 && anyClosed);
    double s0 = std::min(p.pathStart, p.pathEnd), s1 = std::max(p.pathStart, p.pathEnd);
    const double range = s1 - s0;
    const bool fullLoop = singleClosed && range >= 0.999;
    for (A_long i = 0; i < n; i++) {
        double f = (n > 1) ? (fullLoop ? (double)i / (double)n : (double)i / (double)(n - 1)) : 0.0;
        double u = s0 + range * f + p.pathOffset + p.pathSpeed * timeSec;
        if (p.pathReverse) u = 1.0 - u;
        if (p.pathLoop) u -= std::floor(u); else u = std::min(std::max(u, 0.0), 1.0);
        double dist = u * total;
        // 여러 패스: 누적 길이로 어느 패스인지 찾기
        size_t ci = 0; double acc = 0;
        while (ci + 1 < curves.size() && dist > acc + curves[ci].total) { acc += curves[ci].total; ci++; }
        PathPt pt = curves[ci].at(dist - acc);
        if (p.pathReverse) pt.ang += 180.0;
        if (!p.pathAlign) pt.ang = 0; else pt.ang += p.pathAngle;
        out.push_back(pt);
    }
}

// ── 클론 변환 목록 (프리렌더·Bake 공용) ──
// pvx/pvy = 소스 내용 중심(피벗), srcW/H = 내용 크기 (모두 현재 해상도 px). pathPts 는 이미 ds 적용된 버퍼 좌표.
static void BuildClones(const BC_Params& p, double pvx, double pvy, double srcW, double srcH, const std::vector<PathPt>& pathPts, std::vector<BC_Xf>& xf)
{
    A_long n = (p.mode == BC_MODE_GRID) ? p.cols * p.rows : p.count;
    if (p.mode == BC_MODE_PATH) n = (A_long)pathPts.size() > 0 ? (A_long)pathPts.size() : 1;
    if (n < 1) n = 1;
    A_long origin = std::min(std::max(p.origin, (A_long)1), n);
    A_long oc = std::min(std::max(p.originX, (A_long)1), p.cols) - 1, orow = std::min(std::max(p.originY, (A_long)1), p.rows) - 1;
    double cx = p.centerObj ? pvx : p.cx, cy = p.centerObj ? pvy : p.cy;
    const A_long originIdx = (p.mode == BC_MODE_GRID) ? (orow * p.cols + oc) : (p.mode == BC_MODE_LINEAR ? origin - 1 : 0);
    xf.clear(); xf.reserve(n);
    for (A_long i = 0; i < n; i++) {
        const double k = (double)(i - originIdx);
        double px, py, rot = p.rotStep * k, sc = 1.0 + p.scaleStep * k;
        if (p.mode == BC_MODE_GRID) {
            A_long col = i % p.cols, row = i / p.cols;
            px = pvx + (col - oc) * (srcW + p.gapX); py = pvy + (row - orow) * (srcH + p.gapY);
        } else if (p.mode == BC_MODE_PATH) {
            if ((size_t)i < pathPts.size()) { px = pathPts[i].x; py = pathPts[i].y; rot += pathPts[i].ang; }
            else { px = pvx; py = pvy; }
        } else if (p.mode == BC_MODE_RADIAL) {
            double step = (std::fabs(p.sweep) >= 360.0 || n <= 1) ? p.sweep / n : p.sweep / (n - 1);
            double ang = p.startAng + step * i, rad = ang * PI / 180.0;
            px = cx + std::cos(rad) * p.radius; py = cy + std::sin(rad) * p.radius;
            if (p.faceOut) rot += ang + 90.0;
        } else {
            if (p.dir == BC_DIR_V) { px = pvx + k * p.offset; py = pvy + k * (srcH + p.gap); }
            else                   { px = pvx + k * (srcW + p.gap); py = pvy + k * p.offset; }
        }
        if (p.randPos > 0)   { px += Rnd(i, p.seed, 1) * p.randPos; py += Rnd(i, p.seed, 2) * p.randPos; }
        if (p.randRot > 0)   rot += Rnd(i, p.seed, 3) * p.randRot;
        if (p.randScale > 0) sc  += Rnd(i, p.seed, 4) * p.randScale;
        if (sc < 0) sc = 0;
        double r = rot * PI / 180.0, cs = std::cos(r) * sc, sn = std::sin(r) * sc;
        BC_Xf x;
        x.a = cs; x.b = -sn; x.c = sn; x.d = cs;
        x.tx = px - (x.a * pvx + x.b * pvy);
        x.ty = py - (x.c * pvx + x.d * pvy);
        double op = (n > 1) ? (1.0 + (p.opEnd - 1.0) * (double)i / (double)(n - 1)) : 1.0;
        if (p.randOpacity > 0) op *= 1.0 + Rnd(i, p.seed, 5) * p.randOpacity;
        x.opacity = (float)std::min(std::max(op, 0.0), 1.0);
        xf.push_back(x);
    }
}

// ── 프리렌더 ──

static void DeletePreRenderData(void* p) { delete reinterpret_cast<BC_PreRenderData*>(p); }

static PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    BC_Params p;
    ERR(ReadParams(in_data, nullptr, p));
    if (err) return err;
    BC_PreRenderData* d = new BC_PreRenderData();

    double dsx = (double)in_data->downsample_x.num / (double)in_data->downsample_x.den;
    double dsy = (double)in_data->downsample_y.num / (double)in_data->downsample_y.den;
    if (dsx <= 0) dsx = 1;
    if (dsy <= 0) dsy = 1;
    p.gap *= dsx; p.offset *= dsx; p.gapX *= dsx; p.gapY *= dsy; p.radius *= dsx; p.randPos *= dsx;

    // 입력: 모든 클론이 소스 전체를 필요로 하므로 전체를 요청 (AE 가 레이어 최대 영역으로 자름)
    PF_RenderRequest req = extra->input->output_request;
    req.rect.left = -100000; req.rect.top = -100000; req.rect.right = 100000; req.rect.bottom = 100000;
    req.preserve_rgb_of_zero_alpha = FALSE;
    PF_CheckoutResult in_result;
    ERR(extra->cb->checkout_layer(in_data->effect_ref, BC_INPUT, BC_INPUT, &req, in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (err) { delete d; return err; }
    d->in_rect = in_result.result_rect;
    const PF_LRect& m = in_result.max_result_rect;
    const double srcW = (double)(m.right - m.left), srcH = (double)(m.bottom - m.top);
    const double pvx = (m.left + m.right) * 0.5, pvy = (m.top + m.bottom) * 0.5;

    std::vector<PathPt> pathPts;
    if (p.mode == BC_MODE_PATH) {
        std::vector<PathCurve> curves; bool anyClosed = false;
        if (!GatherPathLayerCurves(in_data, BC_PATH_LAYER, curves, anyClosed)) GatherMaskCurve(in_data, p.pathId, curves, anyClosed);
        const double tSec = (double)in_data->current_time / (double)in_data->time_scale;
        SamplePath(curves, anyClosed, p, tSec, p.count, pathPts);
        for (PathPt& pt : pathPts) { pt.x *= dsx; pt.y *= dsy; }
    }
    BuildClones(p, pvx, pvy, srcW, srcH, pathPts, d->xf);

    // 출력 최대 영역 = 각 클론으로 변환한 입력 최대 영역 모서리의 합집합 (+1px)
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    const double cxs[4] = { (double)m.left, (double)m.right, (double)m.left, (double)m.right };
    const double cys[4] = { (double)m.top, (double)m.top, (double)m.bottom, (double)m.bottom };
    for (const BC_Xf& x : d->xf) for (int k = 0; k < 4; k++) {
        double ox = x.a * cxs[k] + x.b * cys[k] + x.tx, oy = x.c * cxs[k] + x.d * cys[k] + x.ty;
        minx = std::min(minx, ox); maxx = std::max(maxx, ox); miny = std::min(miny, oy); maxy = std::max(maxy, oy);
    }
    PF_LRect maxr;
    if (m.right <= m.left || m.bottom <= m.top || minx > maxx) { maxr.left = maxr.top = maxr.right = maxr.bottom = 0; }
    else { maxr.left = (A_long)std::floor(minx) - 1; maxr.top = (A_long)std::floor(miny) - 1; maxr.right = (A_long)std::ceil(maxx) + 1; maxr.bottom = (A_long)std::ceil(maxy) + 1; }
    extra->output->max_result_rect = maxr;
    PF_LRect r = extra->input->output_request.rect;
    r.left = std::max(r.left, maxr.left); r.top = std::max(r.top, maxr.top); r.right = std::min(r.right, maxr.right); r.bottom = std::min(r.bottom, maxr.bottom);
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    extra->output->result_rect = r;
    d->out_rect = r;
    extra->output->solid = FALSE;
    extra->output->pre_render_data = d;
    extra->output->delete_pre_render_data_func = DeletePreRenderData;
    return err;
}

// ── Bake to Layers: 현재 시간의 클론 변환을 ExtendScript 로 넘겨 복제 레이어로 굳힘 ──

static PF_Err BakeToLayers(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    if (!g_registered) return PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    BC_Params p; ReadParams(in_data, params, p);
    AEGP_LayerH meL = NULL; if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &meL) || !meL) return PF_Err_NONE;
    AEGP_CompH compH = NULL; suites.LayerSuite9()->AEGP_GetLayerParentComp(meL, &compH);
    AEGP_ItemH itemH = NULL; A_long compId = 0; if (compH) { suites.CompSuite11()->AEGP_GetItemFromComp(compH, &itemH); if (itemH) suites.ItemSuite9()->AEGP_GetItemID(itemH, &compId); }
    A_long layerIdx = 0; suites.LayerSuite9()->AEGP_GetLayerIndex(meL, &layerIdx);
    A_Time lt = { in_data->current_time, (A_u_long)in_data->time_scale }, ct = lt; suites.LayerSuite9()->AEGP_ConvertLayerToCompTime(meL, &lt, &ct);
    AEGP_ObjectType meType = AEGP_ObjectType_AV; suites.LayerSuite9()->AEGP_GetLayerObjectType(meL, &meType);
    const bool vec = (meType == AEGP_ObjectType_VECTOR || meType == AEGP_ObjectType_TEXT);   // 버퍼 == 컴프 좌표
    // 내용 경계 (레이어 공간) → 버퍼 좌표 (셰이프/텍스트는 레이어→월드 행렬로)
    A_FloatRect b = { 0, 0, (A_FpLong)in_data->width, (A_FpLong)in_data->height };
    suites.LayerSuite9()->AEGP_GetLayerMaskedBounds(meL, AEGP_LTimeMode_CompTime, &ct, &b);
    double srcW = b.right - b.left, srcH = b.bottom - b.top;
    double pvx = (b.left + b.right) * 0.5, pvy = (b.top + b.bottom) * 0.5;
    if (vec) { A_Matrix4 mm; if (!suites.LayerSuite9()->AEGP_GetLayerToWorldXform(meL, &ct, &mm)) { Aff a = AffFromMatrix(mm); a.apply(pvx, pvy); srcW *= std::hypot(a.a, a.b); srcH *= std::hypot(a.c, a.d); } }
    const double halfW = 0, halfH = 0;

    std::vector<PathPt> pathPts;
    if (p.mode == BC_MODE_PATH) {
        std::vector<PathCurve> curves; bool anyClosed = false;
        if (!GatherPathLayerCurves(in_data, BC_PATH_LAYER, curves, anyClosed)) GatherMaskCurve(in_data, p.pathId, curves, anyClosed);
        SamplePath(curves, anyClosed, p, (double)in_data->current_time / (double)in_data->time_scale, p.count, pathPts);
    }
    std::vector<BC_Xf> xf; BuildClones(p, pvx, pvy, srcW, srcH, pathPts, xf);

    // 스크립트: 복제본마다 앵커 = 피벗(레이어 공간), 위치 = 원본이 피벗을 놓았을 자리, 회전·크기·불투명도 가산
    std::string js;
    char buf[512];
    js += "(function(){var comp=app.project.itemByID(" + std::to_string(compId) + ");if(!(comp&&comp instanceof CompItem))return 'nocomp';";
    js += "var L=comp.layer(" + std::to_string(layerIdx + 1) + ");var P=comp.selectedLayers;";
    snprintf(buf, sizeof buf, "var vec=%s;var pvB=[%.4f,%.4f];var pv=vec?L.compPointToSource(pvB):pvB;", vec ? "true" : "false", pvx, pvy); js += buf;
    js += "var data=[";
    for (size_t i = 0; i < xf.size(); i++) {
        const BC_Xf& x = xf[i];
        double sc = std::hypot(x.a, x.c), rot = std::atan2(x.c, x.a) * 180.0 / PI;
        double px = x.a * pvx + x.b * pvy + x.tx, py = x.c * pvx + x.d * pvy + x.ty;   // 피벗이 놓이는 버퍼 좌표
        snprintf(buf, sizeof buf, "%s[%.4f,%.4f,%.4f,%.4f,%.4f]", i ? "," : "", px - halfW, py - halfH, rot, sc, (double)x.opacity); js += buf;
    }
    js += "];app.beginUndoGroup('BANG Cloner Bake');try{";
    js += "var fx=null,par=L.property('ADBE Effect Parade');for(var e=1;e<=par.numProperties;e++)if(par.property(e).matchName==='BANG Cloner')fx=par.property(e);";
    js += "var base=L.property('ADBE Transform Group');var bRot=base.property('ADBE Rotate Z').value,bSc=base.property('ADBE Scale').value,bOp=base.property('ADBE Opacity').value;";
    js += "var made=[];for(var i=data.length-1;i>=0;i--){var d=data[i];var c=L.duplicate();c.name=L.name+' clone '+(i+1);";
    js += "var cp=c.property('ADBE Effect Parade');for(var q=cp.numProperties;q>=1;q--)if(cp.property(q).matchName==='BANG Cloner')cp.property(q).remove();";
    js += "var tg=c.property('ADBE Transform Group');var pos=vec?[d[0],d[1]]:L.sourcePointToComp([d[0],d[1]]);tg.property('ADBE Anchor Point').setValue(pv);";
    js += "var pp=tg.property('ADBE Position');if(pp.dimensionsSeparated){tg.property('ADBE Position_0').setValue(pos[0]);tg.property('ADBE Position_1').setValue(pos[1]);}else{var pv3=pp.value;pv3[0]=pos[0];pv3[1]=pos[1];pp.setValue(pv3);}";
    js += "tg.property('ADBE Rotate Z').setValue(bRot+d[2]);var s2=bSc.slice(0);s2[0]=bSc[0]*d[3];s2[1]=bSc[1]*d[3];tg.property('ADBE Scale').setValue(s2);tg.property('ADBE Opacity').setValue(bOp*d[4]);made.push(c);}";
    js += "if(fx)fx.enabled=false;L.enabled=false;L.shy=true;for(var k=0;k<made.length;k++)made[k].selected=true;L.selected=false;";
    js += "}finally{app.endUndoGroup();}return 'baked '+data.length;})()";

    AEGP_MemHandle resH = NULL, errH = NULL;
    suites.UtilitySuite6()->AEGP_ExecuteScript(g_plugin_id, js.c_str(), FALSE, &resH, &errH);
    if (resH) suites.MemorySuite1()->AEGP_FreeMemHandle(resH);
    if (errH) suites.MemorySuite1()->AEGP_FreeMemHandle(errH);
    return PF_Err_NONE;
}

// ── 커스텀 UI: 퀵 버튼 그리기 / 클릭 ──

struct BtnRect { float x, y, w, h; };
static BtnRect QuickBtnRect(const QuickRow& q, const PF_UnionableRect& frame, int i)
{
    const int cols = q.cols;
    float frameW = (float)(frame.right - frame.left);
    float w = (float)kQuickBtnW;
    if (frameW > 0) w = std::max(12.f, std::min((float)kQuickBtnW, (frameW - (cols - 1) * kQuickBtnGap) / cols));
    BtnRect r;
    r.x = frame.left + (i % cols) * (w + kQuickBtnGap);
    r.y = frame.top + 1 + (i / cols) * (kQuickBtnH + kQuickBtnGap);
    r.w = w; r.h = (float)kQuickBtnH;
    return r;
}

static bool QuickIsActive(const QuickRow& q, PF_ParamDef* params[], int i)
{
    if (q.kind == 3) return q.values[i] == 0 && std::fabs(FIX_2_FLOAT(params[q.target]->u.ad.value) - q.resetTo) < 1e-3;
    if (q.kind == 4) return q.values[i] == 0 && std::fabs(params[q.target]->u.fs_d.value - q.resetTo) < 1e-3;
    A_long cols = params[BC_COLS]->u.sd.value, rows = params[BC_ROWS]->u.sd.value;
    A_long ox = params[BC_ORIGIN_X]->u.sd.value, oy = params[BC_ORIGIN_Y]->u.sd.value;
    auto slot = [](A_long v, A_long n) { if (v <= 1) return 0; if (v >= n) return 2; return (v == (n + 1) / 2) ? 1 : -1; };
    int sx = slot(ox, cols), sy = slot(oy, rows);
    return sx >= 0 && sy >= 0 && (i % 3) == sx && (i / 3) == sy;
}

static void ToUTF16(const char* utf8, DRAWBOT_UTF16Char* out, int cap)
{
#ifdef AE_OS_WIN
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, (wchar_t*)out, cap);
    if (n <= 0) out[0] = 0;
#else
    int k = 0; for (; utf8[k] && k < cap - 1; k++) out[k] = (DRAWBOT_UTF16Char)(unsigned char)utf8[k]; out[k] = 0;
#endif
}

// 굵은 화살표 (mdi arrow-*-thick 느낌): 방향 (dx,dy), 크기 s, 중심 (cx,cy) — 채운 다각형
static void AddThickArrow(DRAWBOT_Suites& db, DRAWBOT_PathRef path, float cx, float cy, float dx, float dy, float s)
{
    float len = std::sqrt(dx * dx + dy * dy); if (len < 1e-6f) return; dx /= len; dy /= len;
    const float px = -dy, py = dx;
    const float tail = -0.50f * s, headBase = 0.02f * s, tip = 0.50f * s;
    const float shaft = 0.17f * s, head = 0.44f * s;
    auto P = [&](float a, float b, float& x, float& y) { x = cx + dx * a + px * b; y = cy + dy * a + py * b; };
    float x, y;
    P(tail, -shaft, x, y);      db.path_suiteP->MoveTo(path, x, y);
    P(headBase, -shaft, x, y);  db.path_suiteP->LineTo(path, x, y);
    P(headBase, -head, x, y);   db.path_suiteP->LineTo(path, x, y);
    P(tip, 0, x, y);            db.path_suiteP->LineTo(path, x, y);
    P(headBase, head, x, y);    db.path_suiteP->LineTo(path, x, y);
    P(headBase, shaft, x, y);   db.path_suiteP->LineTo(path, x, y);
    P(tail, shaft, x, y);       db.path_suiteP->LineTo(path, x, y);
    db.path_suiteP->Close(path);
}

// Origin 버튼 아이콘: 8방향 굵은 화살표, 가운데는 Align Center(십자 + 정사각)
static PF_Err DrawOriginIcon(DRAWBOT_Suites& db, DRAWBOT_SupplierRef sup, DRAWBOT_SurfaceRef surf, DRAWBOT_BrushRef brush, DRAWBOT_PenRef penThin, int i, float cx, float cy, float size)
{
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    DRAWBOT_PathRef path = NULL; ERR(db.supplier_suiteP->NewPath(sup, &path));
    if (err) return err;
    const int col = i % 3, row = i / 3;
    if (col == 1 && row == 1) {
        const float h = size * 0.5f;
        db.path_suiteP->MoveTo(path, cx - h, cy); db.path_suiteP->LineTo(path, cx + h, cy);
        db.path_suiteP->MoveTo(path, cx, cy - h); db.path_suiteP->LineTo(path, cx, cy + h);
        ERR(db.surface_suiteP->StrokePath(surf, penThin, path));
        DRAWBOT_PathRef sq = NULL; ERR(db.supplier_suiteP->NewPath(sup, &sq));
        if (!err) {
            const float q = size * 0.27f;
            DRAWBOT_RectF32 rr = { cx - q, cy - q, q * 2, q * 2 };
            ERR(db.path_suiteP->AddRect(sq, &rr));
            ERR(db.surface_suiteP->FillPath(surf, brush, sq, kDRAWBOT_FillType_Default));
            ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)sq));
        }
    } else {
        AddThickArrow(db, path, cx, cy, (float)(col - 1), (float)(row - 1), size);
        ERR(db.surface_suiteP->FillPath(surf, brush, path, kDRAWBOT_FillType_Default));
    }
    ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path));
    return err;
}

static PF_Err QuickDraw(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_EventExtra* ev, const QuickRow& q)
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
        float fontSize = 11.f; db.supplier_suiteP->GetDefaultFontSize(sup, &fontSize);
        if (q.cols >= 9) fontSize = std::min(fontSize, 9.f);
        DRAWBOT_FontRef font = NULL; ERR(db.supplier_suiteP->NewDefaultFont(sup, fontSize, &font));
        const DRAWBOT_ColorRGBA cFill = { 0.30f, 0.30f, 0.30f, 1 }, cOn = { 0.16f, 0.45f, 0.85f, 1 }, cEdge = { 0.14f, 0.14f, 0.14f, 1 }, cText = { 0.92f, 0.92f, 0.92f, 1 };
        DRAWBOT_BrushRef bFill = NULL, bOn = NULL, bText = NULL; DRAWBOT_PenRef pen = NULL, penIcon = NULL;
        ERR(db.supplier_suiteP->NewBrush(sup, &cFill, &bFill));
        ERR(db.supplier_suiteP->NewBrush(sup, &cOn, &bOn));
        ERR(db.supplier_suiteP->NewBrush(sup, &cText, &bText));
        ERR(db.supplier_suiteP->NewPen(sup, &cEdge, 1.f, &pen));
        ERR(db.supplier_suiteP->NewPen(sup, &cText, 1.5f, &penIcon));
        for (int i = 0; i < q.n && !err; i++) {
            BtnRect r = QuickBtnRect(q, ev->effect_win.current_frame, i);
            DRAWBOT_PathRef path = NULL; ERR(db.supplier_suiteP->NewPath(sup, &path));
            DRAWBOT_RectF32 rr = { r.x + 0.5f, r.y + 0.5f, r.w, r.h };
            ERR(db.path_suiteP->AddRect(path, &rr));
            ERR(db.surface_suiteP->FillPath(surf, QuickIsActive(q, params, i) ? bOn : bFill, path, kDRAWBOT_FillType_Default));
            ERR(db.surface_suiteP->StrokePath(surf, pen, path));
            if (q.kind == 2) {
                ERR(DrawOriginIcon(db, sup, surf, bText, penIcon, i, r.x + r.w * 0.5f + 0.5f, r.y + r.h * 0.5f + 0.5f, std::min(r.w, r.h) * 0.62f));
            } else {
                DRAWBOT_UTF16Char txt[16]; ToUTF16(q.labels[i], txt, 16);
                DRAWBOT_PointF32 org = { r.x + r.w * 0.5f, r.y + r.h * 0.5f + fontSize * 0.36f };
                ERR(db.surface_suiteP->DrawString(surf, bText, font, txt, &org, kDRAWBOT_TextAlignment_Center, kDRAWBOT_TextTruncation_None, 0.f));
            }
            if (path) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path));
        }
        if (penIcon) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)penIcon));
        if (pen)   ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pen));
        if (bText) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bText));
        if (bOn)   ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bOn));
        if (bFill) ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)bFill));
        if (font)  ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)font));
        db.surface_suiteP->PopStateStack(surf);
    }
    ERR2(AEFX_ReleaseDrawbotSuites(in_data, out_data));
    if (!err) ev->evt_out_flags = PF_EO_HANDLED_EVENT;
    return err;
}

static PF_Err QuickClick(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_EventExtra* ev, const QuickRow& q)
{
    PF_Err err = PF_Err_NONE;
    if (ev->effect_win.area != PF_EA_CONTROL) return err;
    const PF_Point pt = ev->u.do_click.screen_point;
    for (int i = 0; i < q.n; i++) {
        BtnRect r = QuickBtnRect(q, ev->effect_win.current_frame, i);
        if (pt.h < r.x || pt.h >= r.x + r.w || pt.v < r.y || pt.v >= r.y + r.h) continue;
        if (q.kind == 3) {
            double cur = FIX_2_FLOAT(params[q.target]->u.ad.value);
            params[q.target]->u.ad.value = FLOAT2FIX((q.values[i] == 0) ? (double)q.resetTo : cur + q.values[i]);
            params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        } else if (q.kind == 4) {
            double cur = params[q.target]->u.fs_d.value;
            params[q.target]->u.fs_d.value = (q.values[i] == 0) ? (double)q.resetTo : cur + q.values[i];
            params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        } else {
            A_long cols = params[BC_COLS]->u.sd.value, rows = params[BC_ROWS]->u.sd.value;
            int sx = i % 3, sy = i / 3;
            params[q.target]->u.sd.value  = (sx == 0) ? 1 : (sx == 1 ? (cols + 1) / 2 : cols);  params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            params[q.target2]->u.sd.value = (sy == 0) ? 1 : (sy == 1 ? (rows + 1) / 2 : rows); params[q.target2]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        }
        PF_Rect inval(ev->effect_win.current_frame);
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        suites.AppSuite4()->PF_InvalidateRect(ev->contextH, &inval);
        ev->evt_out_flags = PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
        break;
    }
    return err;
}

static PF_Err HandleEvent(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_EventExtra* ev)
{
    if (!ev->contextH || (*ev->contextH)->w_type != PF_Window_EFFECT) return PF_Err_NONE;
    const QuickRow* q = FindQuick(ev->effect_win.index);
    if (!q) return PF_Err_NONE;
    switch (ev->e_type) {
        case PF_Event_DRAW:     return QuickDraw(in_data, out_data, params, ev, *q);
        case PF_Event_DO_CLICK: return QuickClick(in_data, out_data, params, ev, *q);
        default: return PF_Err_NONE;
    }
}

// ── ECW 상태: 활성 배치 그룹만 펼침 + 회색 처리, Grid 에선 Count 숨김, Center on Object 면 Center 회색 ──

static PF_Err UpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    if (!g_registered) return PF_Err_NONE;
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const A_long mode = params[BC_MODE]->u.pd.value;

    AEGP_EffectRefH meH = NULL;
    ERR(suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(g_plugin_id, in_data->effect_ref, &meH));
    if (err || !meH) return err;
    {
        AEGP_StreamRefH sH = NULL;
        ERR(suites.StreamSuite2()->AEGP_GetNewEffectStreamByIndex(g_plugin_id, meH, BC_COUNT, &sH));
        if (!err && sH) ERR(suites.DynamicStreamSuite2()->AEGP_SetDynamicStreamFlag(sH, AEGP_DynStreamFlag_HIDDEN, FALSE, mode == BC_MODE_GRID));
        if (sH) ERR2(suites.StreamSuite2()->AEGP_DisposeStream(sH));
    }
    ERR2(suites.EffectSuite2()->AEGP_DisposeEffect(meH));

    // 배치 그룹은 숨기지 않고(숨기면 안의 커스텀 컨트롤 본문이 빈 칸으로 남음 — AE 2026) 활성 그룹만 펼치고 나머지는 접고 회색
    struct G { int idx; int forMode; };
    static const G groups[] = { { BC_G_LINEAR, BC_MODE_LINEAR }, { BC_G_GRID, BC_MODE_GRID }, { BC_G_RADIAL, BC_MODE_RADIAL }, { BC_G_PATH, BC_MODE_PATH } };
    for (const G& g : groups) {
        PF_ParamDef copy = *params[g.idx];
        copy.param_type = PF_Param_GROUP_START;
        if (g.forMode == mode) { copy.flags &= ~PF_ParamFlag_COLLAPSE_TWIRLY; copy.ui_flags &= ~PF_PUI_DISABLED; }
        else                   { copy.flags |=  PF_ParamFlag_COLLAPSE_TWIRLY; copy.ui_flags |=  PF_PUI_DISABLED; }
        ERR2(suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, g.idx, &copy));
    }
    {
        PF_ParamDef copy = *params[BC_CENTER];
        copy.param_type = PF_Param_POINT;
        if (params[BC_CENTER_OBJ]->u.bd.value) copy.ui_flags |= PF_PUI_DISABLED; else copy.ui_flags &= ~PF_PUI_DISABLED;
        ERR2(suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, BC_CENTER, &copy));
    }
    return err;
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], const PF_UserChangedParamExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    if (extra->param_index == BC_MODE || extra->param_index == BC_CENTER_OBJ) {
        err = UpdateParamsUI(in_data, out_data, params);
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    }
    if (extra->param_index == BC_COLS || extra->param_index == BC_ORIGIN_X) {
        A_long cols = params[BC_COLS]->u.sd.value, ox = params[BC_ORIGIN_X]->u.sd.value;
        if (ox > cols) { params[BC_ORIGIN_X]->u.sd.value = cols; params[BC_ORIGIN_X]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE; }
    }
    if (extra->param_index == BC_ROWS || extra->param_index == BC_ORIGIN_Y) {
        A_long rows = params[BC_ROWS]->u.sd.value, oy = params[BC_ORIGIN_Y]->u.sd.value;
        if (oy > rows) { params[BC_ORIGIN_Y]->u.sd.value = rows; params[BC_ORIGIN_Y]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE; }
    }
    if (extra->param_index == BC_BAKE) err = BakeToLayers(in_data, out_data, params);
    return err;
}

// ── 픽셀 타입별 접근 ─────────────────────────────────────────
template <typename P> struct Chan;
template <> struct Chan<PF_Pixel>      { static float get(A_u_char v)  { return v * (1.0f / 255.0f); }   static A_u_char  put(float f) { return (A_u_char)(std::min(std::max(f, 0.f), 1.f) * 255.0f + 0.5f); } };
template <> struct Chan<PF_Pixel16>    { static float get(A_u_short v) { return v * (1.0f / 32768.0f); } static A_u_short put(float f) { return (A_u_short)(std::min(std::max(f, 0.f), 1.f) * 32768.0f + 0.5f); } };
template <> struct Chan<PF_PixelFloat> { static float get(float v)     { return v; }                     static float     put(float f) { return f; } };

struct F4 { float a, r, g, b; };

// 행 범위를 스레드로 분할 (작은 작업은 단일 스레드)
static void ParallelRows(int y0, int y1, size_t workPerRow, const std::function<void(int, int)>& fn)
{
    int rows = y1 - y0;
    if (rows <= 0) return;
    unsigned hw = std::thread::hardware_concurrency();
    int nt = (int)std::min<unsigned>(hw ? hw : 1, 8);
    if (nt < 2 || (size_t)rows * workPerRow < 65536 || rows < nt * 2) { fn(y0, y1); return; }
    std::vector<std::thread> pool;
    int chunk = (rows + nt - 1) / nt;
    for (int t = 0; t < nt; t++) {
        int a = y0 + t * chunk, b = std::min(y1, a + chunk);
        if (a >= b) break;
        pool.emplace_back(fn, a, b);
    }
    for (auto& th : pool) th.join();
}

template <typename P>
static PF_Err RenderClones(const BC_PreRenderData* d, const PF_EffectWorld* in, PF_EffectWorld* out)
{
    const int iw = in->width, ih = in->height, ow = out->width, oh = out->height;

    // 입력 → premultiplied float
    std::vector<F4> src((size_t)iw * ih);
    ParallelRows(0, ih, (size_t)iw, [&](int ya, int yb) {
        for (int y = ya; y < yb; y++) {
            const P* row = (const P*)((const char*)in->data + (size_t)y * in->rowbytes);
            F4* s = &src[(size_t)y * iw];
            for (int x = 0; x < iw; x++) {
                float a = Chan<P>::get(row[x].alpha);
                s[x].a = a; s[x].r = Chan<P>::get(row[x].red) * a; s[x].g = Chan<P>::get(row[x].green) * a; s[x].b = Chan<P>::get(row[x].blue) * a;
            }
        }
    });
    std::vector<F4> dst((size_t)ow * oh, F4{ 0, 0, 0, 0 });

    const double inL = d->in_rect.left, inT = d->in_rect.top;      // 입력 world (0,0) 의 레이어 좌표
    const double outL = d->out_rect.left, outT = d->out_rect.top;  // 출력 world (0,0) 의 레이어 좌표

    for (const BC_Xf& x : d->xf) {
        if (x.opacity <= 0.f) continue;
        double det = x.a * x.d - x.b * x.c;
        if (std::fabs(det) < 1e-12) continue;
        // 이 클론이 덮는 출력 영역 (입력 영역 모서리를 변환)
        double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
        const double cxs[4] = { inL, inL + iw, inL, inL + iw }, cys[4] = { inT, inT, inT + ih, inT + ih };
        for (int k = 0; k < 4; k++) {
            double ox = x.a * cxs[k] + x.b * cys[k] + x.tx, oy = x.c * cxs[k] + x.d * cys[k] + x.ty;
            minx = std::min(minx, ox); maxx = std::max(maxx, ox); miny = std::min(miny, oy); maxy = std::max(maxy, oy);
        }
        int x0 = std::max(0, (int)std::floor(minx - outL) - 1), y0 = std::max(0, (int)std::floor(miny - outT) - 1);
        int x1 = std::min(ow, (int)std::ceil(maxx - outL) + 1), y1 = std::min(oh, (int)std::ceil(maxy - outT) + 1);
        if (x1 <= x0 || y1 <= y0) continue;
        const float op = x.opacity;

        // 빠른 경로: 회전·크기 없음 + 정수 이동 → 픽셀 직접 복사
        const double eps = 1e-6;
        const double dxl = x.tx + inL - outL, dyl = x.ty + inT - outT;   // 입력 (0,0) 이 놓이는 출력 픽셀 좌표
        const bool integerCopy = std::fabs(x.a - 1) < eps && std::fabs(x.d - 1) < eps && std::fabs(x.b) < eps && std::fabs(x.c) < eps &&
                                 std::fabs(dxl - std::round(dxl)) < 1e-3 && std::fabs(dyl - std::round(dyl)) < 1e-3;
        if (integerCopy) {
            const int ox = (int)std::round(dxl), oy = (int)std::round(dyl);
            int cx0 = std::max(0, ox), cy0 = std::max(0, oy), cx1 = std::min(ow, ox + iw), cy1 = std::min(oh, oy + ih);
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            ParallelRows(cy0, cy1, (size_t)(cx1 - cx0), [&](int ya, int yb) {
                for (int py = ya; py < yb; py++) {
                    const F4* s = &src[(size_t)(py - oy) * iw + (cx0 - ox)];
                    F4* o = &dst[(size_t)py * ow + cx0];
                    for (int px = cx0; px < cx1; px++, s++, o++) {
                        if (s->a <= 0.f) continue;
                        float sa = s->a * op, k = 1.f - sa;
                        o->a = sa + o->a * k; o->r = s->r * op + o->r * k; o->g = s->g * op + o->g * k; o->b = s->b * op + o->b * k;
                    }
                }
            });
            continue;
        }

        // 일반 경로: 역행렬 + 증분 바이리니어 (premultiplied)
        const double ia = x.d / det, ib = -x.b / det, ic = -x.c / det, id = x.a / det;
        ParallelRows(y0, y1, (size_t)(x1 - x0) * 4, [&](int ya, int yb) {
            for (int py = ya; py < yb; py++) {
                F4* drow = &dst[(size_t)py * ow];
                const double uy = (outT + py + 0.5) - x.ty;
                const double ux0 = (outL + x0 + 0.5) - x.tx;
                // 소스 레이어 좌표 → 입력 픽셀 좌표 (픽셀 중심 0.5 보정); px 가 1 늘 때마다 (ia, ic) 증가
                double sx = ia * ux0 + ib * uy - inL - 0.5, sy = ic * ux0 + id * uy - inT - 0.5;
                for (int px = x0; px < x1; px++, sx += ia, sy += ic) {
                    const float fsx = (float)sx, fsy = (float)sy;
                    const int fx = (int)std::floor(fsx), fy = (int)std::floor(fsy);
                    if (fx < -1 || fy < -1 || fx >= iw || fy >= ih) continue;
                    const float tx = fsx - fx, ty = fsy - fy;
                    F4 acc = { 0, 0, 0, 0 };
                    const bool x0ok = fx >= 0, x1ok = fx + 1 < iw, y0ok = fy >= 0, y1ok = fy + 1 < ih;
                    #define TAP(XOK, YOK, XX, YY, W) if ((XOK) && (YOK)) { const F4& s = src[(size_t)(YY) * iw + (XX)]; const float w = (W); acc.a += s.a * w; acc.r += s.r * w; acc.g += s.g * w; acc.b += s.b * w; }
                    TAP(x0ok, y0ok, fx, fy, (1 - tx) * (1 - ty)); TAP(x1ok, y0ok, fx + 1, fy, tx * (1 - ty));
                    TAP(x0ok, y1ok, fx, fy + 1, (1 - tx) * ty);   TAP(x1ok, y1ok, fx + 1, fy + 1, tx * ty);
                    #undef TAP
                    if (acc.a <= 0.f) continue;
                    const float sa = acc.a * op, k = 1.f - sa;
                    F4& o = drow[px];
                    o.a = sa + o.a * k; o.r = acc.r * op + o.r * k; o.g = acc.g * op + o.g * k; o.b = acc.b * op + o.b * k;
                }
            }
        });
    }

    // premultiplied → straight 출력
    ParallelRows(0, oh, (size_t)ow, [&](int ya, int yb) {
        for (int y = ya; y < yb; y++) {
            P* orow = (P*)((char*)out->data + (size_t)y * out->rowbytes);
            const F4* s = &dst[(size_t)y * ow];
            for (int x = 0; x < ow; x++) {
                float a = s[x].a, inv = (a > 1e-6f) ? 1.f / a : 0.f;
                orow[x].alpha = Chan<P>::put(a);
                orow[x].red = Chan<P>::put(s[x].r * inv); orow[x].green = Chan<P>::put(s[x].g * inv); orow[x].blue = Chan<P>::put(s[x].b * inv);
            }
        }
    });
    return PF_Err_NONE;
}

static PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    const BC_PreRenderData* d = reinterpret_cast<const BC_PreRenderData*>(extra->input->pre_render_data);
    if (!d) return PF_Err_INTERNAL_STRUCT_DAMAGED;

    PF_EffectWorld* inW = nullptr; PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, BC_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));
    if (err || !outW) return err;

    if (!inW || inW->width <= 0 || inW->height <= 0) {
        AEFX_SuiteScoper<PF_FillMatteSuite2> fm(in_data, kPFFillMatteSuite, kPFFillMatteSuiteVersion2, out_data);
        return fm->fill(in_data->effect_ref, NULL, NULL, outW);
    }

    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
    PF_PixelFormat fmt = PF_PixelFormat_INVALID;
    ERR(ws->PF_GetPixelFormat(outW, &fmt));
    if (err) return err;

    switch (fmt) {
        case PF_PixelFormat_ARGB128: err = RenderClones<PF_PixelFloat>(d, inW, outW); break;
        case PF_PixelFormat_ARGB64:  err = RenderClones<PF_Pixel16>(d, inW, outW);    break;
        default:                     err = RenderClones<PF_Pixel>(d, inW, outW);      break;
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
        "BANG Cloner", "BANG Cloner", "BANG", AE_RESERVED_INFO, "EffectMain",
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
            case PF_Cmd_EVENT:              err = HandleEvent(in_data, out_data, params, (PF_EventExtra*)extra); break;
            case PF_Cmd_SMART_PRE_RENDER:   err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
            case PF_Cmd_SMART_RENDER:       err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
            default: break;
        }
    } catch (PF_Err& thrown) { err = thrown; }
    catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; }
    return err;
}
