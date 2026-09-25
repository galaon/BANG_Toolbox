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
static bool          g_registered = false;

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(BANG_GRAD_MAJOR, BANG_GRAD_MINOR, BANG_GRAD_BUG, BANG_GRAD_STAGE, BANG_GRAD_BUILD);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI;
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
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER("Stops", 2, BG_NUM_STOPS, 2, BG_NUM_STOPS, 3, BG_COUNT);

    TOPIC_OPEN("Color Stops " LINE, BG_G_STOPS);
    for (int i = 0; i < BG_NUM_STOPS; i++) {
        char nm[32];
        sprintf_s(nm, "Color %d", i + 1);
        AEFX_CLR_STRUCT(def);
        PF_ADD_COLOR(nm, kDefault[i][0], kDefault[i][1], kDefault[i][2], BG_S1_COLOR + i * 3);
        sprintf_s(nm, "Position %d", i + 1);
        FSLIDER(nm, 0, 100, 0, 100, DefaultPos(i), PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, BG_S1_POS + i * 3);
        sprintf_s(nm, "Opacity %d", i + 1);
        FSLIDER(nm, 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BG_S1_OP + i * 3);
    }
    TOPIC_END(BG_G_STOPS_END);

    TOPIC_CLOSED("Output " LINE, BG_G_OUT);
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

    out_data->num_params = BG_NUM_PARAMS;
    return err;
}

// ── ECW 상태: 쓰지 않는 정지점과 모양별 항목을 회색으로 ──────
//  (회색 처리하는 곳에 '다시 켜는 컨트롤' 을 두지 않는다 — Shape·Stops 는 바깥에 있다)
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
    for (int i = 0; i < BG_NUM_STOPS; i++) {
        const bool on = i < count;
        grey(BG_S1_COLOR + i * 3, PF_Param_COLOR,        on);
        grey(BG_S1_POS   + i * 3, PF_Param_FLOAT_SLIDER, on);
        grey(BG_S1_OP    + i * 3, PF_Param_FLOAT_SLIDER, on);
    }
    return err;
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], const PF_UserChangedParamExtra* extra)
{
    if (extra->param_index == BG_COUNT) {
        // 정지점 개수를 바꾸면 위치를 고르게 다시 뿌려준다 — 안 그러면 그라데이션이
        // 기본값 범위에서 잘려 보이고, 사용자가 매번 손으로 숫자를 넣어야 한다.
        const int n = std::min(BG_NUM_STOPS, std::max(2, (int)params[BG_COUNT]->u.sd.value));
        for (int i = 0; i < n; i++) {
            PF_ParamDef* pd = params[BG_S1_POS + i * 3];
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
    CHK(BG_DITHER);         d->dither      = pd.u.fs_d.value / 100.0;
    CHK(BG_BLEND);          d->blend       = pd.u.pd.value;
    CHK(BG_AMOUNT);         d->amount      = pd.u.fs_d.value / 100.0;
    CHK(BG_PRESERVE_ALPHA); d->preserveAlpha = pd.u.bd.value != 0;
    for (int i = 0; i < BG_NUM_STOPS; i++) {
        CHK(BG_S1_COLOR + i * 3);
        d->stops[i].r = pd.u.cd.value.red / 255.0; d->stops[i].g = pd.u.cd.value.green / 255.0; d->stops[i].b = pd.u.cd.value.blue / 255.0;
        CHK(BG_S1_POS + i * 3);   d->stops[i].pos     = pd.u.fs_d.value / 100.0;
        CHK(BG_S1_OP  + i * 3);   d->stops[i].opacity = pd.u.fs_d.value / 100.0;
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

    for (int y = 0; y < oh; y++) {
        P* orow = (P*)((char*)out->data + (size_t)y * out->rowbytes);
        const int iy = y + offy;
        const P* irow = (in && iy >= 0 && iy < ih) ? (const P*)((const char*)in->data + (size_t)iy * in->rowbytes) : nullptr;
        const float py = (float)(d->out_rect.top + y) + 0.5f;
        for (int x = 0; x < ow; x++) {
            const int ix = x + offx;
            float ba = 0, brr = 0, bgg = 0, bbb = 0;            // 원본 (straight)
            if (irow && ix >= 0 && ix < iw) {
                const P& p = irow[ix];
                ba = Chan<P>::get(p.alpha); brr = Chan<P>::get(p.red); bgg = Chan<P>::get(p.green); bbb = Chan<P>::get(p.blue);
            }
            const float px = (float)(d->out_rect.left + x) + 0.5f;

            // 1) 모양 → t
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
                    const int cx = ix, cy = iy;
                    const float dist = (!contour.empty() && cx >= 0 && cx < iw && cy >= 0 && cy < ih)
                                     ? contour[(size_t)cy * iw + cx] : 0.f;
                    t = dist / span;
                    break;
                }
                default:               t = ((px - sx) * dx + (py - sy) * dy) * invLen2; break;   // Linear
            }

            // 2) 반복·위상·방향
            t = t * cycles + phase;
            if (dither > 0.f) t += (Hash01(x + (int)d->out_rect.left, y + (int)d->out_rect.top) - 0.5f) * dither * (1.f / 255.f);
            switch (d->repeatMode) {
                case BG_REPEAT_REPEAT: t = t - std::floor(t); break;
                case BG_REPEAT_MIRROR: { float f = std::fabs(t); float m = std::fmod(f, 2.f); t = (m > 1.f) ? 2.f - m : m; break; }
                default: t = std::min(1.f, std::max(0.f, t)); break;
            }
            if (d->reverse) t = 1.f - t;

            // 3) 정지점 → 색·불투명도
            const BG_Stop* S = d->stops;
            const int n = (int)d->count;
            RGB col; float ga;
            if (t <= S[0].pos) {
                col = { (float)S[0].r, (float)S[0].g, (float)S[0].b }; ga = (float)S[0].opacity;
            } else if (t >= S[n - 1].pos) {
                col = { (float)S[n - 1].r, (float)S[n - 1].g, (float)S[n - 1].b }; ga = (float)S[n - 1].opacity;
            } else {
                int k = 0;
                while (k < n - 2 && t > S[k + 1].pos) k++;
                const float p0 = (float)S[k].pos, p1 = (float)S[k + 1].pos;
                float u = (p1 - p0 > 1e-6f) ? (t - p0) / (p1 - p0) : 0.f;
                if (smooth > 0.f) u = u + (u * u * (3.f - 2.f * u) - u) * smooth;   // 선형 ↔ smoothstep
                const RGB A = { (float)S[k].r, (float)S[k].g, (float)S[k].b };
                const RGB B = { (float)S[k + 1].r, (float)S[k + 1].g, (float)S[k + 1].b };
                col = MixColor(d->interp, A, B, u);
                ga = (float)(S[k].opacity + (S[k + 1].opacity - S[k].opacity) * u);
            }
            ga *= amount;
            if (d->preserveAlpha) ga *= ba;

            // 4) 원본 위에 premultiplied 로 합성
            float gr = col.r, gg = col.g, gb = col.b;
            if (d->blend != BG_BLEND_NORMAL && ba > 0.f) BlendRGB(d->blend, brr, bgg, bbb, gr, gg, gb);
            float oa = ba, orr = brr * ba, og = bgg * ba, ob = bbb * ba;
            const float k = 1.f - ga;
            oa = ga + oa * k; orr = gr * ga + orr * k; og = gg * ga + og * k; ob = gb * ga + ob * k;

            P& o = orow[x];
            const float inv = (oa > 1e-6f) ? 1.f / oa : 0.f;
            o.alpha = Chan<P>::put(oa); o.red = Chan<P>::put(orr * inv); o.green = Chan<P>::put(og * inv); o.blue = Chan<P>::put(ob * inv);
        }
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
            case PF_Cmd_SMART_PRE_RENDER:   err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
            case PF_Cmd_SMART_RENDER:       err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
            default: break;
        }
    } catch (PF_Err& thrown) { err = thrown; }
    catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; }
    return err;
}
