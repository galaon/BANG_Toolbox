// BANG_Stroke.cpp — BANG Stroke
//  알파 경계까지의 부호 있는 거리(Euclidean Distance Transform)를 한 번 계산하고,
//  |d - 중심| < 두께/2 인 픽셀에 획 색을 칠한다. 모서리는 자연스럽게 둥글고, 계산량은 이미지 크기에 비례.
//  · 출력 버퍼를 (두께+오프셋+부드러움) 만큼 넓혀 레이어 경계 밖의 바깥 획도 잘리지 않음
//  · 8 / 16 / 32bpc, SmartFX, 멀티프레임 렌더링 지원
//  · AE 이펙트 버퍼는 straight alpha — 합성도 straight 로 수행

#include "BANG_Stroke.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#ifdef AE_OS_WIN
#include <windows.h>
#endif

// ── 디버그 로그 (BANG_FX_LOG 정의 시 %TEMP%\bang_stroke.log 에 기록) ──
#ifdef BANG_FX_LOG
static void LOGF(const char* fmt, ...) {
    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path); strcat_s(path, "bang_stroke.log");
    FILE* f = nullptr; if (fopen_s(&f, path, "a") || !f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc(10, f); fclose(f);
}
#else
#define LOGF(...) ((void)0)
#endif

// ── 명령 처리 ────────────────────────────────────────────────

static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "BANG Stroke v%d.%d\rAlpha-edge distance stroke - BANG_Toolbox",
        BANG_STROKE_MAJOR, BANG_STROKE_MINOR);
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(BANG_STROKE_MAJOR, BANG_STROKE_MINOR, BANG_STROKE_BUG, BANG_STROKE_STAGE, BANG_STROKE_BUILD);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_REVEALS_ZERO_ALPHA;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Position", 3, BS_POS_OUTSIDE, "Outside|Center|Inside", BS_DISK_POSITION);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Width", 0, 1000, 0, 60, 6, PF_Precision_TENTHS, 0, 0, BS_DISK_WIDTH);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Offset", -500, 500, -20, 20, 0, PF_Precision_TENTHS, 0, 0, BS_DISK_OFFSET);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color", PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8, BS_DISK_COLOR);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, BS_DISK_OPACITY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Softness", 0, 200, 0, 20, 0, PF_Precision_TENTHS, 0, 0, BS_DISK_SOFTNESS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Body", 2, BS_BODY_KEEP, "Keep|Hide (stroke only)", BS_DISK_BODY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Order", 2, BS_ORDER_BEHIND, "Stroke Behind|Stroke In Front", BS_DISK_ORDER);

    out_data->num_params = BS_NUM_PARAMS;
    return err;
}

// ── 프리렌더: 파라미터 읽기, 여백 계산, 입력 체크아웃, 출력 영역 확장 ──

static void DeletePreRenderData(void* p) { delete reinterpret_cast<BS_PreRenderData*>(p); }

static PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef pd;

    BS_PreRenderData* d = new BS_PreRenderData();
    memset(d, 0, sizeof(*d));

    #define CHK(idx) AEFX_CLR_STRUCT(pd); ERR(PF_CHECKOUT_PARAM(in_data, idx, in_data->current_time, in_data->time_step, in_data->time_scale, &pd));
    CHK(BS_POSITION); d->position = pd.u.pd.value;
    CHK(BS_WIDTH);    d->width    = pd.u.fs_d.value;
    CHK(BS_OFFSET);   d->offset   = pd.u.fs_d.value;
    CHK(BS_COLOR);    d->color    = pd.u.cd.value;
    CHK(BS_OPACITY);  d->opacity  = pd.u.fs_d.value / 100.0;
    CHK(BS_SOFTNESS); d->softness = pd.u.fs_d.value;
    CHK(BS_BODY);     d->body     = pd.u.pd.value;
    CHK(BS_ORDER);    d->order    = pd.u.pd.value;
    #undef CHK

    // 다운샘플(Draft/해상도) 보정: 파라미터는 풀해상도 px 기준
    PF_FpLong dsx = (PF_FpLong)in_data->downsample_x.num / (PF_FpLong)in_data->downsample_x.den;
    PF_FpLong dsy = (PF_FpLong)in_data->downsample_y.num / (PF_FpLong)in_data->downsample_y.den;
    PF_FpLong ds = std::min(dsx, dsy);
    if (ds <= 0) ds = 1;
    d->width    *= ds; d->offset *= ds; d->softness *= ds;

    // 바깥으로 뻗는 최대 거리 = 오프셋 + 두께(위치에 따라) + 부드러움 + 여유
    PF_FpLong reach = std::max(0.0, d->offset) + d->width + d->softness + 2.0;
    if (d->position == BS_POS_INSIDE) reach = std::max(0.0, d->offset) + d->softness + 2.0;   // 안쪽 획은 밖으로 안 나감
    d->margin = (A_long)std::ceil(reach);
    LOGF("PreRender pos=%ld width=%.1f offset=%.1f soft=%.1f margin=%ld req=[%ld %ld %ld %ld] bitdepth=%d", d->position, d->width, d->offset, d->softness, d->margin, extra->input->output_request.rect.left, extra->input->output_request.rect.top, extra->input->output_request.rect.right, extra->input->output_request.rect.bottom, (int)extra->input->bitdepth);

    // 출력: 요청 영역을 그대로 만들되, 최대 영역은 입력 최대 영역 + 여백
    PF_RenderRequest req = extra->input->output_request;
    PF_CheckoutResult in_result;
    // 입력은 출력 요청 영역 + 여백 (거리 계산에 이웃 알파가 필요)
    req.rect.left   -= d->margin; req.rect.top    -= d->margin;
    req.rect.right  += d->margin; req.rect.bottom += d->margin;
    req.preserve_rgb_of_zero_alpha = FALSE;

    ERR(extra->cb->checkout_layer(in_data->effect_ref, BS_INPUT, BS_INPUT, &req,
                                  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (!err) {
        d->in_rect = in_result.result_rect;    // 실제로 받게 될 입력 영역 (레이어 경계로 잘린 결과)
        LOGF("  in_result=[%ld %ld %ld %ld] max=[%ld %ld %ld %ld]", in_result.result_rect.left, in_result.result_rect.top, in_result.result_rect.right, in_result.result_rect.bottom, in_result.max_result_rect.left, in_result.max_result_rect.top, in_result.max_result_rect.right, in_result.max_result_rect.bottom);

        PF_LRect maxr = in_result.max_result_rect;
        maxr.left -= d->margin; maxr.top -= d->margin; maxr.right += d->margin; maxr.bottom += d->margin;
        extra->output->max_result_rect = maxr;

        // result = 요청 ∩ 최대
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
    } else {
        delete d;
    }
    return err;
}

// ── 거리 변환 (Felzenszwalb & Huttenlocher, 1D 제곱거리 두 번) ──
static void edt1d(const float* f, float* dOut, int n, std::vector<int>& v, std::vector<float>& z)
{
    const float INF = 1e20f;
    v.resize(n); z.resize(n + 1);
    int k = 0; v[0] = 0; z[0] = -INF; z[1] = INF;
    for (int q = 1; q < n; q++) {
        float s;
        while (true) {
            int vk = v[k];
            s = ((f[q] + q * (float)q) - (f[vk] + vk * (float)vk)) / (2.0f * (q - vk));
            if (s <= z[k]) { k--; if (k < 0) { k = 0; break; } } else break;
        }
        if (k == 0 && s <= z[0]) { v[0] = q; z[0] = -INF; z[1] = INF; continue; }
        k++; v[k] = q; z[k] = s; z[k + 1] = INF;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        int vk = v[k];
        dOut[q] = (q - vk) * (float)(q - vk) + f[vk];
    }
}

// grid: 0 = 씨앗(거리 0), INF = 나머지. 결과: 씨앗까지의 제곱 거리
static void edt2d(std::vector<float>& g, int w, int h)
{
    std::vector<float> col(std::max(w, h)), out(std::max(w, h));
    std::vector<int> v; std::vector<float> z;
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) col[y] = g[y * w + x];
        edt1d(col.data(), out.data(), h, v, z);
        for (int y = 0; y < h; y++) g[y * w + x] = out[y];
    }
    for (int y = 0; y < h; y++) {
        edt1d(&g[y * w], out.data(), w, v, z);
        memcpy(&g[y * w], out.data(), w * sizeof(float));
    }
}

// ── 픽셀 타입별 접근 ─────────────────────────────────────────
template <typename P> struct Chan;
template <> struct Chan<PF_Pixel>      { static float get(A_u_char v)  { return v / 255.0f; }   static A_u_char  put(float f) { return (A_u_char)(std::min(std::max(f, 0.f), 1.f) * 255.0f + 0.5f); } };
template <> struct Chan<PF_Pixel16>    { static float get(A_u_short v) { return v / 32768.0f; } static A_u_short put(float f) { return (A_u_short)(std::min(std::max(f, 0.f), 1.f) * 32768.0f + 0.5f); } };
template <> struct Chan<PF_PixelFloat> { static float get(float v)     { return v; }            static float     put(float f) { return f; } };

struct StrokeCtx {
    const BS_PreRenderData* d;
    const PF_EffectWorld* in; const PF_EffectWorld* out;
    PF_LRect out_rect;                  // 출력 world (0,0) 이 대응하는 레이어 좌표
    // 거리장 격자: 출력 영역을 여백만큼 더 넓힌 범위 (입력 밖은 투명으로 취급)
    int gx0, gy0, gw, gh;               // 격자 원점(레이어 좌표)과 크기
    std::vector<float> sdf;             // 부호 있는 거리 (+ 바깥, − 안쪽), 격자 크기
    std::vector<float> alpha;           // 격자 크기의 알파
};

template <typename P>
static void BuildSDF(StrokeCtx& c)
{
    const int pad = c.d->margin + 1;
    c.gx0 = c.out_rect.left - pad; c.gy0 = c.out_rect.top - pad;
    c.gw = (c.out_rect.right - c.out_rect.left) + pad * 2;
    c.gh = (c.out_rect.bottom - c.out_rect.top) + pad * 2;
    const int w = c.gw, h = c.gh;
    const float INF = 1e20f;
    std::vector<float> inside((size_t)w * h), outside((size_t)w * h);
    c.alpha.assign((size_t)w * h, 0.f);
    const int iw = c.in->width, ih = c.in->height;
    const int inx0 = c.d->in_rect.left, iny0 = c.d->in_rect.top;
    for (int y = 0; y < h; y++) {
        int iy = (c.gy0 + y) - iny0;
        const P* row = (iy >= 0 && iy < ih) ? (const P*)((const char*)c.in->data + iy * c.in->rowbytes) : nullptr;
        for (int x = 0; x < w; x++) {
            int ix = (c.gx0 + x) - inx0;
            float a = (row && ix >= 0 && ix < iw) ? Chan<P>::get(row[ix].alpha) : 0.f;
            c.alpha[(size_t)y * w + x] = a;
            bool fg = a >= 0.5f;
            inside[(size_t)y * w + x]  = fg ? INF : 0.f;   // 씨앗 = 배경 → 전경 픽셀의 "밖까지 거리"
            outside[(size_t)y * w + x] = fg ? 0.f : INF;   // 씨앗 = 전경 → 배경 픽셀의 "안까지 거리"
        }
    }
    edt2d(inside, w, h); edt2d(outside, w, h);
    c.sdf.resize((size_t)w * h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        float a = c.alpha[i];
        float dIn = std::sqrt(inside[i]), dOut = std::sqrt(outside[i]);
        // 전경 픽셀은 배경까지 거리(dIn), 배경 픽셀은 전경까지 거리(dOut)만 유효 (자기 자신은 항상 씨앗이라 0)
        bool fg = (a >= 0.5f);
        float d = fg ? -(dIn - 0.5f) : (dOut - 0.5f);
        if ((fg && dIn <= 1.0f) || (!fg && dOut <= 1.0f)) d = 0.5f - a;   // 경계 픽셀은 알파 커버리지로 서브픽셀 보정
        c.sdf[i] = d;
    }
}

template <typename P>
static PF_Err RenderStroke(StrokeCtx& c)
{
    const BS_PreRenderData* d = c.d;
    const int ow = c.out->width, oh = c.out->height;
    const int iw = c.in->width,  ih = c.in->height;
    const int offx = c.out_rect.left - d->in_rect.left;    // 출력 (0,0) → 입력 픽셀 좌표 오프셋
    const int offy = c.out_rect.top  - d->in_rect.top;
    const int gox = c.out_rect.left - c.gx0, goy = c.out_rect.top - c.gy0;   // 출력 (0,0) → 격자 좌표

    const float half = (float)(d->width * 0.5);
    float center;   // 획 중심의 거리값
    switch (d->position) {
        case BS_POS_INSIDE: center = -(float)(d->offset) - half; break;
        case BS_POS_CENTER: center = (float)d->offset; break;
        default:            center = (float)d->offset + half; break;
    }
    const float soft = (float)d->softness;
    const float sr = Chan<PF_Pixel>::get(d->color.red), sg = Chan<PF_Pixel>::get(d->color.green), sb = Chan<PF_Pixel>::get(d->color.blue);
    const float op = (float)d->opacity;
    const bool hideBody = (d->body == BS_BODY_HIDE);
    // 안쪽·중앙 획은 본체 위에 그려야 보인다(Layer Style 과 동일). '합성 순서'는 바깥 획에만 의미가 있음
    const bool strokeFront = (d->order == BS_ORDER_FRONT) || (d->position != BS_POS_OUTSIDE);

    for (int y = 0; y < oh; y++) {
        P* orow = (P*)((char*)c.out->data + y * c.out->rowbytes);
        int iy = y + offy;
        const P* irow = (iy >= 0 && iy < ih) ? (const P*)((const char*)c.in->data + iy * c.in->rowbytes) : nullptr;
        const float* srow = &c.sdf[(size_t)(y + goy) * c.gw + gox];
        for (int x = 0; x < ow; x++) {
            int ix = x + offx;
            float ba = 0, br = 0, bg = 0, bb = 0;
            if (irow && ix >= 0 && ix < iw) {
                const P& p = irow[ix];
                ba = Chan<P>::get(p.alpha); br = Chan<P>::get(p.red); bg = Chan<P>::get(p.green); bb = Chan<P>::get(p.blue);
            }
            float dist = srow[x];
            if (hideBody) ba = 0;
            // 획 커버리지: t = half - |dist - center| (px). 0.5px AA + 부드러움
            float t = half - std::fabs(dist - center);
            float cov = (t + 0.5f + soft * 0.5f) / (1.0f + soft);
            cov = std::min(std::max(cov, 0.f), 1.f);
            float sa = cov * op;

            float oa, orr, og, ob;
            if (sa <= 0.f) { oa = ba; orr = br; og = bg; ob = bb; }
            else if (strokeFront) {
                oa = sa + ba * (1 - sa);
                float wS = sa, wB = ba * (1 - sa);
                orr = (sr * wS + br * wB) / oa; og = (sg * wS + bg * wB) / oa; ob = (sb * wS + bb * wB) / oa;
            } else {
                oa = ba + sa * (1 - ba);
                float wB = ba, wS = sa * (1 - ba);
                if (oa > 0) { orr = (br * wB + sr * wS) / oa; og = (bg * wB + sg * wS) / oa; ob = (bb * wB + sb * wS) / oa; }
                else { orr = og = ob = 0; }
            }
            P& o = orow[x];
            o.alpha = Chan<P>::put(oa); o.red = Chan<P>::put(orr); o.green = Chan<P>::put(og); o.blue = Chan<P>::put(ob);
        }
    }
    return PF_Err_NONE;
}

static PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    const BS_PreRenderData* d = reinterpret_cast<const BS_PreRenderData*>(extra->input->pre_render_data);
    if (!d) return PF_Err_INTERNAL_STRUCT_DAMAGED;

    PF_EffectWorld* inW = nullptr; PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, BS_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));
    if (err || !outW) return err;

    StrokeCtx c; c.d = d; c.in = inW; c.out = outW;
    LOGF("SmartRender in=%ldx%ld out=%ldx%ld out_rect=[%ld %ld %ld %ld]", inW ? inW->width : -1, inW ? inW->height : -1, outW->width, outW->height, d->out_rect.left, d->out_rect.top, d->out_rect.right, d->out_rect.bottom);
    c.out_rect = d->out_rect;   // 출력 world (0,0) = result_rect.left/top
    if (!inW || inW->width <= 0 || inW->height <= 0) {
        // 입력이 비어 있으면(레이어 밖만 요청) 투명으로
        AEFX_SuiteScoper<PF_FillMatteSuite2> fm(in_data, kPFFillMatteSuite, kPFFillMatteSuiteVersion2, out_data);
        return fm->fill(in_data->effect_ref, NULL, NULL, outW);
    }

    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
    PF_PixelFormat fmt = PF_PixelFormat_INVALID;
    ERR(ws->PF_GetPixelFormat(outW, &fmt));
    if (err) return err;
    LOGF("  fmt=%d", (int)fmt);

    switch (fmt) {
        case PF_PixelFormat_ARGB128: BuildSDF<PF_PixelFloat>(c); err = RenderStroke<PF_PixelFloat>(c); break;
        case PF_PixelFormat_ARGB64:  BuildSDF<PF_Pixel16>(c);    err = RenderStroke<PF_Pixel16>(c);    break;
        default:                     BuildSDF<PF_Pixel>(c);      err = RenderStroke<PF_Pixel>(c);      break;
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
        "BANG Stroke",                       // Name
        "BANG Stroke",                       // Match Name
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
            case PF_Cmd_ABOUT:            err = About(in_data, out_data, params, output); break;
            case PF_Cmd_GLOBAL_SETUP:     err = GlobalSetup(in_data, out_data, params, output); break;
            case PF_Cmd_PARAMS_SETUP:     err = ParamsSetup(in_data, out_data, params, output); break;
            case PF_Cmd_SMART_PRE_RENDER: err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
            case PF_Cmd_SMART_RENDER:     err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
            default: break;
        }
    } catch (PF_Err& thrown) { err = thrown; LOGF("EXC cmd=%d err=%d", (int)cmd, (int)err); }
    catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; LOGF("EXC cmd=%d (unknown)", (int)cmd); }
    if (err) LOGF("cmd=%d err=%d", (int)cmd, (int)err);
    return err;
}
