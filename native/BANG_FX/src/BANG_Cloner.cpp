// BANG_Cloner.cpp — BANG Cloner
//  소스 레이어에 적용하는 인스턴스 클로너. 현재 프레임의 입력 이미지를 N개의 변환(이동·회전·크기)으로 합성한다.
//  · 레이어 복제가 없으므로 개수와 무관하게 소스 애니메이션 타이밍이 정확히 유지된다 (Motion Tile 모델)
//  · 출력 버퍼를 모든 클론의 경계 합집합으로 확장 (PF_OutFlag_I_EXPAND_BUFFER)
//  · 배치: Linear / Grid / Radial. 간격은 "이웃 클론 경계 사이 px(Gap)" — 소스 크기와 무관하게 조절, 음수 = 겹침
//  · 원본 위치 기준: Linear 는 Origin Index(몇 번째가 원본인지), Grid 는 Grid Origin(9방향 칸), Radial 은 Center
//  · 단계 변환(회전·크기·불투명도) + 랜덤(위치·회전·크기, 시드). 배치 모드에 맞지 않는 항목은 숨김 (AEGP DynamicStream HIDDEN)
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
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER | PF_OutFlag_SEND_UPDATE_PARAMS_UI;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_REVEALS_ZERO_ALPHA;
    if (!g_registered && in_data->appl_id != kAppID_Premiere) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        if (suites.UtilitySuite3()->AEGP_RegisterWithAEGP(NULL, "BANG Cloner", &g_plugin_id) == A_Err_NONE) g_registered = true;
    }
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Layout", 3, BC_MODE_LINEAR, "Linear|Grid|Radial", PF_ParamFlag_SUPERVISE, BC_MODE);

    // Linear
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Count", 1, 1000, 1, 50, 5, BC_COUNT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Origin Index", 1, 1000, 1, 50, 1, BC_ORIGIN);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Direction", 2, BC_DIR_H, "Horizontal|Vertical", BC_DIR);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, 0, BC_GAP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Offset", -10000, 10000, -500, 500, 0, PF_Precision_TENTHS, 0, 0, BC_OFFSET);

    // Grid
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Columns", 1, 100, 1, 20, 3, BC_COLS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Rows", 1, 100, 1, 20, 3, BC_ROWS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap X", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, 0, BC_GAP_X);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap Y", -10000, 10000, -200, 200, 20, PF_Precision_TENTHS, 0, 0, BC_GAP_Y);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Grid Origin", 9, 5, "Top Left|Top|Top Right|Left|Center|Right|Bottom Left|Bottom|Bottom Right", BC_GRID_ORIGIN);

    // Radial
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Radius", -10000, 10000, 0, 1000, 200, PF_Precision_TENTHS, 0, 0, BC_RADIUS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Start Angle", 0, BC_START_ANGLE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Sweep", -3600, 3600, 0, 360, 360, PF_Precision_TENTHS, 0, 0, BC_SWEEP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Face Outward", FALSE, 0, BC_FACE_OUT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Center", 50, 50, 0, BC_CENTER);

    // Steps / random
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Rotation Step", 0, BC_ROT_STEP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Scale Step", -1000, 1000, -50, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, BC_SCALE_STEP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("End Opacity", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, BC_OPACITY_END);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Random Position", 0, 10000, 0, 500, 0, PF_Precision_TENTHS, 0, 0, BC_RAND_POS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Random Rotation", 0, 180, 0, 180, 0, PF_Precision_TENTHS, 0, 0, BC_RAND_ROT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Random Scale", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, BC_RAND_SCALE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Seed", 0, 9999, 0, 100, 0, BC_SEED);

    out_data->num_params = BC_NUM_PARAMS;
    return err;
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

    struct Vis { int idx; bool linear, grid, radial; };
    static const Vis table[] = {
        { BC_COUNT,       true,  false, true  },
        { BC_ORIGIN,      true,  false, false },
        { BC_DIR,         true,  false, false },
        { BC_GAP,         true,  false, false },
        { BC_OFFSET,      true,  false, false },
        { BC_COLS,        false, true,  false },
        { BC_ROWS,        false, true,  false },
        { BC_GAP_X,       false, true,  false },
        { BC_GAP_Y,       false, true,  false },
        { BC_GRID_ORIGIN, false, true,  false },
        { BC_RADIUS,      false, false, true  },
        { BC_START_ANGLE, false, false, true  },
        { BC_SWEEP,       false, false, true  },
        { BC_FACE_OUT,    false, false, true  },
        { BC_CENTER,      false, false, true  },
    };
    for (const Vis& v : table) {
        bool show;
        if (mode == BC_MODE_GRID) show = v.grid;
        else if (mode == BC_MODE_RADIAL) show = v.radial;
        else show = v.linear;
        AEGP_StreamRefH sH = NULL;
        ERR(suites.StreamSuite2()->AEGP_GetNewEffectStreamByIndex(g_plugin_id, meH, v.idx, &sH));
        if (!err && sH) ERR(suites.DynamicStreamSuite2()->AEGP_SetDynamicStreamFlag(sH, AEGP_DynStreamFlag_HIDDEN, FALSE, !show));
        if (sH) ERR2(suites.StreamSuite2()->AEGP_DisposeStream(sH));
    }
    ERR2(suites.EffectSuite2()->AEGP_DisposeEffect(meH));
    return err;
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], const PF_UserChangedParamExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    if (extra->param_index == BC_MODE) {
        err = UpdateParamsUI(in_data, out_data, params);
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
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

    A_long mode = BC_MODE_LINEAR, count = 5, origin = 1, dir = BC_DIR_H, cols = 3, rows = 3, gridOrigin = 5, faceOut = 0, seed = 0;
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
    CHK(BC_GRID_ORIGIN); gridOrigin = pd.u.pd.value;
    CHK(BC_RADIUS);      radius     = pd.u.fs_d.value;
    CHK(BC_START_ANGLE); startAng   = FIX_2_FLOAT(pd.u.ad.value);
    CHK(BC_SWEEP);       sweep      = pd.u.fs_d.value;
    CHK(BC_FACE_OUT);    faceOut    = pd.u.bd.value;
    CHK(BC_CENTER);      cx = FIX_2_FLOAT(pd.u.td.x_value); cy = FIX_2_FLOAT(pd.u.td.y_value);
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

    // 클론 변환 목록
    A_long n = (mode == BC_MODE_GRID) ? cols * rows : count;
    if (n < 1) n = 1;
    if (origin < 1) origin = 1;
    if (origin > n) origin = n;
    // Grid Origin (9방향) → 원본이 놓이는 칸
    A_long oc = 0, orow = 0;
    {
        int gi = (int)gridOrigin - 1;
        if (gi < 0 || gi > 8) gi = 4;
        int hx = gi % 3, vy = gi / 3;   // 0 left/top, 1 center, 2 right/bottom
        if (hx == 0) oc = 0; else if (hx == 1) oc = (cols - 1) / 2; else oc = cols - 1;
        if (vy == 0) orow = 0; else if (vy == 1) orow = (rows - 1) / 2; else orow = rows - 1;
    }
    d->xf.reserve(n);
    for (A_long i = 0; i < n; i++) {
        double px, py, rot = rotStep * i, sc = 1.0 + scaleStep * i;
        if (mode == BC_MODE_GRID) {
            A_long col = i % cols, row = i / cols;
            px = pvx + (col - oc) * (srcW + gapX);
            py = pvy + (row - orow) * (srcH + gapY);
        } else if (mode == BC_MODE_RADIAL) {
            double step = (std::fabs(sweep) >= 360.0 || n <= 1) ? sweep / n : sweep / (n - 1);
            double ang = startAng + step * i;
            double rad = ang * PI / 180.0;
            px = cx + std::cos(rad) * radius; py = cy + std::sin(rad) * radius;
            if (faceOut) rot += ang + 90.0;
        } else {
            // Linear: 원본(Origin Index)을 기준으로 앞뒤로 진행. Gap = 이웃 경계 사이 거리
            double k = (double)(i - (origin - 1));
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
            case PF_Cmd_SMART_PRE_RENDER:   err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
            case PF_Cmd_SMART_RENDER:       err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
            default: break;
        }
    } catch (PF_Err& thrown) { err = thrown; }
    catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; }
    return err;
}
