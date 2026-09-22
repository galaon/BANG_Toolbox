// BANG_Cloner.cpp — BANG Cloner
//  소스 레이어에 적용하는 인스턴스 클로너. 현재 프레임의 입력 이미지를 N개의 변환(이동·회전·크기)으로 합성한다.
//  · 레이어 복제가 없으므로 개수와 무관하게 소스 애니메이션 타이밍이 정확히 유지된다 (Motion Tile 모델)
//  · 출력 버퍼를 모든 클론의 경계 합집합으로 확장 (PF_OutFlag_I_EXPAND_BUFFER)
//  · 배치: Linear / Grid / Radial / Path(이 레이어의 마스크 패스를 따라 길이 기준 균등 배치, Align to Path). 간격은 "이웃 클론 경계 사이 px(Gap)" — 소스 크기와 무관하게 조절, 음수 = 겹침
//  · 원본 위치 기준: Linear 는 Origin Index(몇 번째가 원본인지), Grid 는 Origin X/Y(원본이 놓이는 칸), Radial 은 Center
//  · 단계 변환(회전·크기·불투명도) + 랜덤(위치·회전·크기, 시드). 파라미터는 그룹(Linear/Grid/Radial/Step/Random)으로 묶고
//    배치 모드에 맞는 그룹만 펼치고 나머지는 접음(PF_UpdateParamUI). Grid 에선 Count 숨김 (AEGP DynamicStream HIDDEN)
//  · 퀵 버튼(커스텀 ECW UI, Drawbot): Start Angle·Rotation Step·Sweep 은 증감 Preset(가운데 = 리셋: 0 / 0 / 360), Grid Origin 은 3×3
//  · 8 / 16 / 32bpc, SmartFX, 멀티프레임 렌더링
//  · 렌더: 입력을 premultiplied float 로 한 번 변환 → 클론마다 (정수 이동이면 직접 복사, 아니면 증분 바이리니어) over 합성.
//    클론별 행 범위를 스레드로 분할.

#include "BANG_Cloner.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
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
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "BANG Cloner v%d.%d\rInstance cloner (Linear / Grid / Radial) - BANG_Toolbox",
        BANG_CLONER_MAJOR, BANG_CLONER_MINOR);
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(BANG_CLONER_MAJOR, BANG_CLONER_MINOR, BANG_CLONER_BUG, BANG_CLONER_STAGE, BANG_CLONER_BUILD);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER | PF_OutFlag_SEND_UPDATE_PARAMS_UI | PF_OutFlag_CUSTOM_UI;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_REVEALS_ZERO_ALPHA |
                           PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;   // 그룹 flags=0 → 기본 펼침
    if (!g_registered && in_data->appl_id != kAppID_Premiere) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        if (suites.UtilitySuite3()->AEGP_RegisterWithAEGP(NULL, "BANG Cloner", &g_plugin_id) == A_Err_NONE) g_registered = true;
    }
    return PF_Err_NONE;
}

// ── 퀵 버튼 정의 (커스텀 ECW UI) ──
// kind: 0 = 각도 절대값 · 1 = float 절대값 · 2 = Origin 3×3 (대상 X/Y 두 개) · 3 = 각도 증감 · 4 = float 증감 (값 0 인 버튼은 resetTo 로 리셋)
struct QuickRow { int param; int kind; int target; int target2; int n; const float* values; const char* const* labels; int cols; float resetTo; };
static const float  kAngleDelta[]  = { -360, -90, -15, -2.5f, -1, 0, 1, 2.5f, 15, 90, 360 };
static const char* const kAngleDeltaLbls[] = { "-360", "-90", "-15", "-2.5", "-1", "0", "+1", "+2.5", "+15", "+90", "+360" };
static const char* const kSweepDeltaLbls[] = { "-360", "-90", "-15", "-2.5", "-1", "360", "+1", "+2.5", "+15", "+90", "+360" };   // 가운데 = 360 으로 리셋
static const float  kOriginVals[]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };   // 3×3 인덱스: 열 = i%3, 행 = i/3 (0 first · 1 center · 2 last)
static const char* const kOriginLbls[] = { "\xE2\x86\x96", "\xE2\x86\x91", "\xE2\x86\x97", "\xE2\x86\x90", "\xE2\x97\x8F", "\xE2\x86\x92", "\xE2\x86\x99", "\xE2\x86\x93", "\xE2\x86\x98" };
static const QuickRow kQuick[] = {
    { BC_ORIGIN_QUICK, 2, BC_ORIGIN_X,    BC_ORIGIN_Y, 9,  kOriginVals, kOriginLbls,     3,  0 },
    { BC_START_QUICK,  3, BC_START_ANGLE, -1,          11, kAngleDelta, kAngleDeltaLbls, 11, 0 },
    { BC_SWEEP_QUICK,  4, BC_SWEEP,       -1,          11, kAngleDelta, kSweepDeltaLbls, 11, 360 },
    { BC_ROT_QUICK,    3, BC_ROT_STEP,    -1,          11, kAngleDelta, kAngleDeltaLbls, 11, 0 },
};
static int QuickRows(const QuickRow& q) { return (q.n + q.cols - 1) / q.cols; }
static const QuickRow* FindQuick(int param) { for (const QuickRow& q : kQuick) if (q.param == param) return &q; return nullptr; }
static const int kQuickBtnH = 22, kQuickBtnGap = 3, kQuickBtnW = 22;   // 정사각 버튼

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    // 퀵 버튼 행: 데이터 없는 파라미터 + 커스텀 컨트롤(PF_PUI_CONTROL) — 그리기/클릭은 PF_Cmd_EVENT
    // (PF_Param_NO_DATA 는 숨긴 그룹에서도 행이 남아 체크박스 데이터형 + 커스텀 컨트롤로 만든다. 값은 쓰지 않음, 키프레임 불가)
    #define QUICK_ROW(NAME, ID, ROWS) do { AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_CANNOT_TIME_VARY; def.ui_flags = PF_PUI_CONTROL; def.ui_width = 300; def.ui_height = kQuickBtnH * (ROWS) + kQuickBtnGap * ((ROWS) - 1) + 2; PF_ADD_CHECKBOX(NAME, "", FALSE, 0, ID); } while (0)
    #define TOPIC(NAME, ID)  do { AEFX_CLR_STRUCT(def); PF_ADD_TOPIC(NAME, ID); } while (0)
    // 처음 적용 시 Linear 만 펼치고 나머지는 접힌 채 시작 (PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG 가 flags 를 존중)
    #define TOPIC_CLOSED(NAME, ID) do { AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_START_COLLAPSED; PF_ADD_TOPIC(NAME, ID); } while (0)
    #define TOPIC_END(ID)    do { AEFX_CLR_STRUCT(def); PF_END_TOPIC(ID); } while (0)

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Layout", 4, BC_MODE_LINEAR, "Linear|Grid|Radial|Path", PF_ParamFlag_SUPERVISE, BC_MODE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Count", 1, 1000, 1, 50, 5, BC_COUNT);

    TOPIC("Linear " "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", BC_G_LINEAR);   // 이름 + 실선 (31바이트 제한: 선 8칸)
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Origin Index", 1, 1000, 1, 50, 1, BC_ORIGIN);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Direction", 2, BC_DIR_H, "Horizontal|Vertical", BC_DIR);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap (px)", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, 0, BC_GAP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Offset (px)", -10000, 10000, -500, 500, 0, PF_Precision_TENTHS, 0, 0, BC_OFFSET);
    TOPIC_END(BC_G_LINEAR_END);

    TOPIC_CLOSED("Grid " "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", BC_G_GRID);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Columns", 1, 100, 1, 20, 3, BC_COLS);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Rows", 1, 100, 1, 20, 3, BC_ROWS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap X (px)", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, 0, BC_GAP_X);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap Y (px)", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, 0, BC_GAP_Y);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Origin X", 1, 100, 1, 20, 2, BC_ORIGIN_X);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Origin Y", 1, 100, 1, 20, 2, BC_ORIGIN_Y);
    QUICK_ROW("Origin Preset", BC_ORIGIN_QUICK, 3);
    TOPIC_END(BC_G_GRID_END);

    TOPIC_CLOSED("Radial " "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", BC_G_RADIAL);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Radius (px)", -10000, 10000, 0, 1000, 200, PF_Precision_TENTHS, 0, 0, BC_RADIUS);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;   // 다이얼 접힘 (숨긴 그룹의 다이얼이 ECW 에 남는 현상 방지)
    PF_ADD_ANGLE("Start Angle", 0, BC_START_ANGLE);
    QUICK_ROW("Start Angle Preset", BC_START_QUICK, 1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Sweep (deg)", -3600, 3600, 0, 360, 360, PF_Precision_TENTHS, 0, 0, BC_SWEEP);
    QUICK_ROW("Sweep Preset", BC_SWEEP_QUICK, 1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Face Outward", FALSE, 0, BC_FACE_OUT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Center on Object", TRUE, PF_ParamFlag_SUPERVISE, BC_CENTER_OBJ);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Center", 50, 50, 0, BC_CENTER);
    TOPIC_END(BC_G_RADIAL_END);

    TOPIC_CLOSED("Path " "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", BC_G_PATH);
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_PATH;
    PF_STRCPY(def.PF_DEF_NAME, "Mask Path");
    def.u.path_d.dephault = 0;
    def.uu.id = BC_PATH;
    ERR(PF_ADD_PARAM(in_data, -1, &def));
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Align to Path", TRUE, 0, BC_PATH_ALIGN);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Path Offset", -100, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, BC_PATH_OFFSET);
    TOPIC_END(BC_G_PATH_END);

    TOPIC_CLOSED("Step " "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", BC_G_STEP);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    PF_ADD_ANGLE("Rotation Step", 0, BC_ROT_STEP);
    QUICK_ROW("Rotation Step Preset", BC_ROT_QUICK, 1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Scale Step", -1000, 1000, -50, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, BC_SCALE_STEP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("End Opacity", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, BC_OPACITY_END);
    TOPIC_END(BC_G_STEP_END);

    TOPIC_CLOSED("Random " "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", BC_G_RANDOM);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Seed", 0, 9999, 0, 100, 0, BC_SEED);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Random Position (px)", 0, 10000, 0, 500, 0, PF_Precision_TENTHS, 0, 0, BC_RAND_POS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Random Rotation (deg)", 0, 180, 0, 180, 0, PF_Precision_TENTHS, 0, 0, BC_RAND_ROT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Random Scale", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, BC_RAND_SCALE);
    TOPIC_END(BC_G_RANDOM_END);
    #undef QUICK_ROW
    #undef TOPIC
    #undef TOPIC_CLOSED
    #undef TOPIC_END

    // 커스텀 UI(ECW 이벤트) 등록
    PF_CustomUIInfo ci;
    AEFX_CLR_STRUCT(ci);
    ci.events = PF_CustomEFlag_EFFECT;
    ci.comp_ui_alignment = ci.layer_ui_alignment = ci.preview_ui_alignment = PF_UIAlignment_NONE;
    err = (*(in_data->inter.register_ui))(in_data->effect_ref, &ci);

    out_data->num_params = BC_NUM_PARAMS;
    return err;
}

// ── 커스텀 UI: 퀵 버튼 그리기 / 클릭 ──

struct BtnRect { float x, y, w, h; };
static BtnRect QuickBtnRect(const QuickRow& q, const PF_UnionableRect& frame, int i)
{
    const int cols = q.cols;
    float frameW = (float)(frame.right - frame.left);
    float w = (float)kQuickBtnW;   // 정사각. 프레임이 좁으면 균등 축소
    if (frameW > 0) w = std::max(12.f, std::min((float)kQuickBtnW, (frameW - (cols - 1) * kQuickBtnGap) / cols));
    BtnRect r;
    r.x = frame.left + (i % cols) * (w + kQuickBtnGap);
    r.y = frame.top + 1 + (i / cols) * (kQuickBtnH + kQuickBtnGap);
    r.w = w; r.h = (float)kQuickBtnH;
    return r;
}

// 버튼 i 가 현재 값과 같은지 (강조 표시용)
static bool QuickIsActive(const QuickRow& q, PF_ParamDef* params[], int i)
{
    if (q.kind == 0) return std::fabs(FIX_2_FLOAT(params[q.target]->u.ad.value) - q.values[i]) < 1e-3;
    if (q.kind == 1) return std::fabs(params[q.target]->u.fs_d.value - q.values[i]) < 1e-3;
    if (q.kind == 3) return q.values[i] == 0 && std::fabs(FIX_2_FLOAT(params[q.target]->u.ad.value) - q.resetTo) < 1e-3;   // 리셋 버튼만 현재 값이 리셋값일 때 강조
    if (q.kind == 4) return q.values[i] == 0 && std::fabs(params[q.target]->u.fs_d.value - q.resetTo) < 1e-3;
    // Origin 3×3: 현재 Origin X/Y 가 어느 칸인지
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
    const float px = -dy, py = dx;                       // 수직 방향
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
        // 십자선 + 가운데 정사각
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
        const float dx = (float)(col - 1), dy = (float)(row - 1);
        AddThickArrow(db, path, cx, cy, dx, dy, size);
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
        if (q.cols >= 9) fontSize = std::min(fontSize, 9.f);   // 한 줄 11개(Preset 증감)는 작은 글자
        DRAWBOT_FontRef font = NULL; ERR(db.supplier_suiteP->NewDefaultFont(sup, fontSize, &font));
        const DRAWBOT_ColorRGBA cFill = { 0.30f, 0.30f, 0.30f, 1 }, cOn = { 0.16f, 0.45f, 0.85f, 1 }, cEdge = { 0.14f, 0.14f, 0.14f, 1 }, cText = { 0.92f, 0.92f, 0.92f, 1 };
        DRAWBOT_BrushRef bFill = NULL, bOn = NULL, bText = NULL; DRAWBOT_PenRef pen = NULL;
        ERR(db.supplier_suiteP->NewBrush(sup, &cFill, &bFill));
        ERR(db.supplier_suiteP->NewBrush(sup, &cOn, &bOn));
        ERR(db.supplier_suiteP->NewBrush(sup, &cText, &bText));
        ERR(db.supplier_suiteP->NewPen(sup, &cEdge, 1.f, &pen));
        DRAWBOT_PenRef penIcon = NULL; ERR(db.supplier_suiteP->NewPen(sup, &cText, 1.5f, &penIcon));
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
        if (q.kind == 0) {
            params[q.target]->u.ad.value = FLOAT2FIX(q.values[i]);
            params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        } else if (q.kind == 3) {
            // 증감: 현재 각도에 더하기 / 빼기, 값 0 버튼은 resetTo 로 리셋
            double cur = FIX_2_FLOAT(params[q.target]->u.ad.value);
            double nv = (q.values[i] == 0) ? (double)q.resetTo : cur + q.values[i];
            params[q.target]->u.ad.value = FLOAT2FIX(nv);
            params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        } else if (q.kind == 4) {
            double cur = params[q.target]->u.fs_d.value;
            params[q.target]->u.fs_d.value = (q.values[i] == 0) ? (double)q.resetTo : cur + q.values[i];
            params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        } else if (q.kind == 1) {
            params[q.target]->u.fs_d.value = q.values[i];
            params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        } else {
            A_long cols = params[BC_COLS]->u.sd.value, rows = params[BC_ROWS]->u.sd.value;
            int sx = i % 3, sy = i / 3;
            A_long ox = (sx == 0) ? 1 : (sx == 1 ? (cols + 1) / 2 : cols);
            A_long oy = (sy == 0) ? 1 : (sy == 1 ? (rows + 1) / 2 : rows);
            params[q.target]->u.sd.value = ox;  params[q.target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            params[q.target2]->u.sd.value = oy; params[q.target2]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
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



// ── 배치 모드별 파라미터 표시/숨김 (AE 는 PF_PUI_INVISIBLE 을 동적으로 못 바꾸므로 스트림 플래그 사용) ──

static PF_Err UpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    if (!g_registered) return PF_Err_NONE;
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const A_long mode = params[BC_MODE]->u.pd.value;

    AEGP_EffectRefH meH = NULL;
    ERR(suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(g_plugin_id, in_data->effect_ref, &meH));
    if (err || !meH) return err;

    // Grid 에서는 Count 를 숨김 (리프 스트림 숨김은 정상 동작)
    {
        AEGP_StreamRefH sH = NULL;
        ERR(suites.StreamSuite2()->AEGP_GetNewEffectStreamByIndex(g_plugin_id, meH, BC_COUNT, &sH));
        if (!err && sH) ERR(suites.DynamicStreamSuite2()->AEGP_SetDynamicStreamFlag(sH, AEGP_DynStreamFlag_HIDDEN, FALSE, mode == BC_MODE_GRID));
        if (sH) ERR2(suites.StreamSuite2()->AEGP_DisposeStream(sH));
    }
    // 배치 그룹은 숨기지 않고 활성 그룹만 펼치고 나머지는 접는다.
    // (숨기면 그룹 안 커스텀 컨트롤(퀵 버튼)의 본문 영역이 빈 칸으로 남는다 — 접기/높이 변경으로도 제거 불가, AE 2026 동작)
    struct G { int idx; int forMode; };
    static const G groups[] = { { BC_G_LINEAR, BC_MODE_LINEAR }, { BC_G_GRID, BC_MODE_GRID }, { BC_G_RADIAL, BC_MODE_RADIAL }, { BC_G_PATH, BC_MODE_PATH } };
    for (const G& g : groups) {
        PF_ParamDef copy = *params[g.idx];
        copy.param_type = PF_Param_GROUP_START;
        if (g.forMode == mode) { copy.flags &= ~PF_ParamFlag_COLLAPSE_TWIRLY; copy.ui_flags &= ~PF_PUI_DISABLED; }
        else                   { copy.flags |=  PF_ParamFlag_COLLAPSE_TWIRLY; copy.ui_flags |=  PF_PUI_DISABLED; }   // 비활성 그룹은 접고 회색으로
        ERR2(suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, g.idx, &copy));
    }
    ERR2(suites.EffectSuite2()->AEGP_DisposeEffect(meH));

    // Center on Object 가 켜져 있으면 Center 포인트는 회색
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
    // Origin X ≤ Columns, Origin Y ≤ Rows 로 고정 (어느 쪽을 바꿔도)
    if (extra->param_index == BC_COLS || extra->param_index == BC_ORIGIN_X) {
        A_long cols = params[BC_COLS]->u.sd.value, ox = params[BC_ORIGIN_X]->u.sd.value;
        if (ox > cols) { params[BC_ORIGIN_X]->u.sd.value = cols; params[BC_ORIGIN_X]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE; }
    }
    if (extra->param_index == BC_ROWS || extra->param_index == BC_ORIGIN_Y) {
        A_long rows = params[BC_ROWS]->u.sd.value, oy = params[BC_ORIGIN_Y]->u.sd.value;
        if (oy > rows) { params[BC_ORIGIN_Y]->u.sd.value = rows; params[BC_ORIGIN_Y]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE; }
    }
    return err;
}

// ── 결정적 난수: 인덱스·시드·용도별 [-1, 1] ──
static float Rnd(uint32_t i, uint32_t seed, uint32_t salt)
{
    uint32_t h = (i + 1u) * 0x9E3779B1u ^ (seed + 1u) * 0x85EBCA77u ^ (salt + 1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

// ── 프리렌더: 파라미터 → 클론 변환 목록, 입력 체크아웃, 출력 영역 = 모든 클론의 합집합 ──

static void DeletePreRenderData(void* p) { delete reinterpret_cast<BC_PreRenderData*>(p); }

static PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef pd;
    BC_PreRenderData* d = new BC_PreRenderData();

    A_long mode = BC_MODE_LINEAR, count = 5, origin = 1, dir = BC_DIR_H, cols = 3, rows = 3, originX = 2, originY = 2, faceOut = 0, seed = 0;
    A_long centerObj = 1, pathId = 0, pathAlign = 1; double pathOffset = 0;
    double gap = 0, offset = 0, gapX = 0, gapY = 0, radius = 0, startAng = 0, sweep = 360;
    double cx = 0, cy = 0, rotStep = 0, scaleStep = 0, opEnd = 1, randPos = 0, randRot = 0, randScale = 0;

    #define CHK(idx) AEFX_CLR_STRUCT(pd); ERR(PF_CHECKOUT_PARAM(in_data, idx, in_data->current_time, in_data->time_step, in_data->time_scale, &pd));
    CHK(BC_MODE);        mode       = pd.u.pd.value;
    CHK(BC_COUNT);       count      = pd.u.sd.value;
    CHK(BC_ORIGIN);      origin     = pd.u.sd.value;
    CHK(BC_DIR);         dir        = pd.u.pd.value;
    CHK(BC_GAP);         gap        = pd.u.fs_d.value;
    CHK(BC_OFFSET);      offset     = pd.u.fs_d.value;
    CHK(BC_COLS);        cols       = pd.u.sd.value;
    CHK(BC_ROWS);        rows       = pd.u.sd.value;
    CHK(BC_GAP_X);       gapX       = pd.u.fs_d.value;
    CHK(BC_GAP_Y);       gapY       = pd.u.fs_d.value;
    CHK(BC_ORIGIN_X);    originX    = pd.u.sd.value;
    CHK(BC_ORIGIN_Y);    originY    = pd.u.sd.value;
    CHK(BC_RADIUS);      radius     = pd.u.fs_d.value;
    CHK(BC_START_ANGLE); startAng   = FIX_2_FLOAT(pd.u.ad.value);
    CHK(BC_SWEEP);       sweep      = pd.u.fs_d.value;
    CHK(BC_FACE_OUT);    faceOut    = pd.u.bd.value;
    CHK(BC_CENTER_OBJ);  centerObj  = pd.u.bd.value;
    CHK(BC_CENTER);      cx = FIX_2_FLOAT(pd.u.td.x_value); cy = FIX_2_FLOAT(pd.u.td.y_value);
    CHK(BC_PATH);        pathId     = pd.u.path_d.path_id;
    CHK(BC_PATH_ALIGN);  pathAlign  = pd.u.bd.value;
    CHK(BC_PATH_OFFSET); pathOffset = pd.u.fs_d.value / 100.0;
    CHK(BC_ROT_STEP);    rotStep    = FIX_2_FLOAT(pd.u.ad.value);
    CHK(BC_SCALE_STEP);  scaleStep  = pd.u.fs_d.value / 100.0;
    CHK(BC_OPACITY_END); opEnd      = pd.u.fs_d.value / 100.0;
    CHK(BC_RAND_POS);    randPos    = pd.u.fs_d.value;
    CHK(BC_RAND_ROT);    randRot    = pd.u.fs_d.value;
    CHK(BC_RAND_SCALE);  randScale  = pd.u.fs_d.value / 100.0;
    CHK(BC_SEED);        seed       = pd.u.sd.value;
    #undef CHK
    if (err) { delete d; return err; }

    // 다운샘플 보정: px 파라미터는 풀해상도 기준, 포인트 파라미터는 이미 다운샘플 좌표
    double dsx = (double)in_data->downsample_x.num / (double)in_data->downsample_x.den;
    double dsy = (double)in_data->downsample_y.num / (double)in_data->downsample_y.den;
    if (dsx <= 0) dsx = 1;
    if (dsy <= 0) dsy = 1;
    gap *= dsx; offset *= dsx; gapX *= dsx; gapY *= dsy;
    radius *= dsx; randPos *= dsx;

    // 입력: 모든 클론이 소스 전체를 필요로 하므로 전체를 요청 (AE 가 레이어 최대 영역으로 자름)
    PF_RenderRequest req = extra->input->output_request;
    req.rect.left = -100000; req.rect.top = -100000; req.rect.right = 100000; req.rect.bottom = 100000;
    req.preserve_rgb_of_zero_alpha = FALSE;
    PF_CheckoutResult in_result;
    ERR(extra->cb->checkout_layer(in_data->effect_ref, BC_INPUT, BC_INPUT, &req,
                                  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (err) { delete d; return err; }
    d->in_rect = in_result.result_rect;

    // 소스 내용 경계: 피벗(회전·크기의 기준, 원본 자리) 과 크기(Gap 계산용)
    const PF_LRect& m = in_result.max_result_rect;
    const double srcW = (double)(m.right - m.left), srcH = (double)(m.bottom - m.top);
    const double pvx = (m.left + m.right) * 0.5, pvy = (m.top + m.bottom) * 0.5;

    if (centerObj) { cx = pvx; cy = pvy; }   // Radial: 원 중심 = 소스 내용 중심

    // Path: 이 레이어의 마스크 패스를 길이 기준으로 균등 샘플 (좌표는 풀해상도 레이어 px → 다운샘플 보정)
    struct PathPt { double x, y, ang; };
    std::vector<PathPt> pathPts;
    if (mode == BC_MODE_PATH && pathId != 0) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        PF_PathOutlinePtr pathP = NULL;
        PF_Err perr = suites.PathQuerySuite1()->PF_CheckoutPath(in_data->effect_ref, pathId, in_data->current_time, in_data->time_step, in_data->time_scale, &pathP);
        if (!perr && pathP) {
            PF_Boolean openB = FALSE; A_long nseg = 0;
            suites.PathDataSuite1()->PF_PathIsOpen(in_data->effect_ref, pathP, &openB);
            suites.PathDataSuite1()->PF_PathNumSegments(in_data->effect_ref, pathP, &nseg);
            std::vector<PF_PathSegPrepPtr> preps(nseg, nullptr);
            std::vector<double> lens(nseg, 0.0);
            double total = 0;
            for (A_long sIdx = 0; sIdx < nseg; sIdx++) {
                suites.PathDataSuite1()->PF_PathPrepareSegLength(in_data->effect_ref, pathP, sIdx, 24, &preps[sIdx]);
                PF_FpLong L = 0; suites.PathDataSuite1()->PF_PathGetSegLength(in_data->effect_ref, pathP, sIdx, &preps[sIdx], &L);
                lens[sIdx] = L; total += L;
            }
            // 셰이프/텍스트 레이어는 패스 좌표 원점이 레이어 중심(앵커)이라 절반 크기만큼 빼 준다 (솔리드/푸티지는 좌상단 원점)
            double pathOx = 0, pathOy = 0;
            {
                AEGP_LayerH layerH = NULL; AEGP_ObjectType ot = AEGP_ObjectType_AV;
                if (!suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &layerH) && layerH &&
                    !suites.LayerSuite9()->AEGP_GetLayerObjectType(layerH, &ot) &&
                    (ot == AEGP_ObjectType_VECTOR || ot == AEGP_ObjectType_TEXT)) {
                    pathOx = (double)in_data->width / dsx * 0.5; pathOy = (double)in_data->height / dsy * 0.5;
                }
            }
            A_long nP = count < 1 ? 1 : count;
            for (A_long i = 0; i < nP && total > 0; i++) {
                double t = (openB && nP > 1) ? (double)i / (double)(nP - 1) : (double)i / (double)nP;
                t += pathOffset; t -= std::floor(t);
                if (openB && !(pathOffset != 0)) t = std::min(t, 1.0);
                double dist = t * total;
                A_long sIdx = 0;
                while (sIdx < nseg - 1 && dist > lens[sIdx]) { dist -= lens[sIdx]; sIdx++; }
                if (dist > lens[sIdx]) dist = lens[sIdx];
                PF_FpLong x = 0, y = 0, ddx = 1, ddy = 0;
                suites.PathDataSuite1()->PF_PathEvalSegLengthDeriv1(in_data->effect_ref, pathP, &preps[sIdx], sIdx, dist, &x, &y, &ddx, &ddy);
                PathPt pt; pt.x = (x - pathOx) * dsx; pt.y = (y - pathOy) * dsy; pt.ang = pathAlign ? std::atan2(ddy, ddx) * 180.0 / PI : 0.0;
                pathPts.push_back(pt);
            }
            for (A_long sIdx = 0; sIdx < nseg; sIdx++) if (preps[sIdx]) suites.PathDataSuite1()->PF_PathCleanupSegLength(in_data->effect_ref, pathP, sIdx, &preps[sIdx]);
            suites.PathQuerySuite1()->PF_CheckinPath(in_data->effect_ref, pathId, FALSE, pathP);
        }
    }

    // 클론 변환 목록
    A_long n = (mode == BC_MODE_GRID) ? cols * rows : count;
    if (mode == BC_MODE_PATH) n = (A_long)pathPts.size() > 0 ? (A_long)pathPts.size() : 1;
    if (n < 1) n = 1;
    if (origin < 1) origin = 1;
    if (origin > n) origin = n;
    // Origin X/Y (1 기준) → 원본이 놓이는 칸 (Columns/Rows 로 제한)
    A_long oc = std::min(std::max(originX, (A_long)1), cols) - 1, orow = std::min(std::max(originY, (A_long)1), rows) - 1;
    d->xf.reserve(n);
    // 단계(회전·크기)는 원본을 0 으로 두고 앞뒤로 누적 (Linear: Origin Index, Grid: Origin X/Y 칸, Radial: 첫 클론)
    const A_long originIdx = (mode == BC_MODE_GRID) ? (orow * cols + oc) : (mode == BC_MODE_LINEAR ? origin - 1 : 0);
    for (A_long i = 0; i < n; i++) {
        const double k = (double)(i - originIdx);
        double px, py, rot = rotStep * k, sc = 1.0 + scaleStep * k;
        if (mode == BC_MODE_GRID) {
            A_long col = i % cols, row = i / cols;
            px = pvx + (col - oc) * (srcW + gapX);
            py = pvy + (row - orow) * (srcH + gapY);
        } else if (mode == BC_MODE_PATH) {
            if ((size_t)i < pathPts.size()) { px = pathPts[i].x; py = pathPts[i].y; rot += pathPts[i].ang; }
            else { px = pvx; py = pvy; }   // 마스크가 없으면 원본 자리에 그대로
        } else if (mode == BC_MODE_RADIAL) {
            double step = (std::fabs(sweep) >= 360.0 || n <= 1) ? sweep / n : sweep / (n - 1);
            double ang = startAng + step * i;
            double rad = ang * PI / 180.0;
            px = cx + std::cos(rad) * radius; py = cy + std::sin(rad) * radius;
            if (faceOut) rot += ang + 90.0;
        } else {
            // Linear: 원본(Origin Index)을 기준으로 앞뒤로 진행. Gap = 이웃 경계 사이 거리
            if (dir == BC_DIR_V) { px = pvx + k * offset; py = pvy + k * (srcH + gap); }
            else                 { px = pvx + k * (srcW + gap); py = pvy + k * offset; }
        }
        if (randPos > 0)   { px += Rnd(i, seed, 1) * randPos; py += Rnd(i, seed, 2) * randPos; }
        if (randRot > 0)   rot += Rnd(i, seed, 3) * randRot;
        if (randScale > 0) sc  += Rnd(i, seed, 4) * randScale;
        if (sc < 0) sc = 0;
        double r = rot * PI / 180.0, cs = std::cos(r) * sc, sn = std::sin(r) * sc;
        BC_Xf x;
        // 출력 = R·S·(소스 − 피벗) + 위치
        x.a = cs; x.b = -sn; x.c = sn; x.d = cs;
        x.tx = px - (x.a * pvx + x.b * pvy);
        x.ty = py - (x.c * pvx + x.d * pvy);
        x.opacity = (n > 1) ? (float)(1.0 + (opEnd - 1.0) * (double)i / (double)(n - 1)) : 1.0f;
        d->xf.push_back(x);
    }

    // 출력 최대 영역 = 각 클론으로 변환한 입력 최대 영역 모서리의 합집합 (+1px 바이리니어 여유)
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    const double cxs[4] = { (double)m.left, (double)m.right, (double)m.left, (double)m.right };
    const double cys[4] = { (double)m.top, (double)m.top, (double)m.bottom, (double)m.bottom };
    for (const BC_Xf& x : d->xf) {
        for (int k = 0; k < 4; k++) {
            double ox = x.a * cxs[k] + x.b * cys[k] + x.tx, oy = x.c * cxs[k] + x.d * cys[k] + x.ty;
            minx = std::min(minx, ox); maxx = std::max(maxx, ox); miny = std::min(miny, oy); maxy = std::max(maxy, oy);
        }
    }
    PF_LRect maxr;
    if (m.right <= m.left || m.bottom <= m.top || minx > maxx) { maxr.left = maxr.top = maxr.right = maxr.bottom = 0; }
    else {
        maxr.left = (A_long)std::floor(minx) - 1; maxr.top = (A_long)std::floor(miny) - 1;
        maxr.right = (A_long)std::ceil(maxx) + 1;  maxr.bottom = (A_long)std::ceil(maxy) + 1;
    }
    extra->output->max_result_rect = maxr;

    PF_LRect r = extra->input->output_request.rect;
    r.left = std::max(r.left, maxr.left);   r.top = std::max(r.top, maxr.top);
    r.right = std::min(r.right, maxr.right); r.bottom = std::min(r.bottom, maxr.bottom);
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    extra->output->result_rect = r;
    d->out_rect = r;
    extra->output->solid = FALSE;
    extra->output->pre_render_data = d;
    extra->output->delete_pre_render_data_func = DeletePreRenderData;
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
