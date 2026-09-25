// BANG_Stroke.cpp — BANG Stroke
//  알파 경계까지의 부호 있는 거리(Euclidean Distance Transform)를 한 번 계산하고,
//  |d - 중심| < 두께/2 인 픽셀에 획 색을 칠한다. 계산량은 이미지 크기에 비례.
//  · 모서리: Round(거리장 그대로) / Miter / Bevel — 최근접 씨앗을 공유하는 픽셀들의 방향 팬에서
//    인접 두 변의 법선을 복원해 거리를 다시 쓴다 (SharpenCorners)
//  · 획 하나 = 이펙트 하나. 여러 겹은 **이 이펙트를 여러 번 적용**하면 된다 —
//    두 번째 인스턴스는 첫 획이 포함된 알파를 입력으로 받으므로 그 바깥 윤곽을 따라 그려진다.
//  · 색: 단색 또는 그라데이션(Across Stroke / Linear 각도 / Radial 내용 중심) · 블렌드(Normal/Multiply/Screen/Add)
//  · Edge Noise: 거리장에 fBm 값 노이즈를 더해 가장자리를 거칠게 (Evolution 으로 애니메이션)
//  · 출력 버퍼를 (오프셋+두께+부드러움+노이즈) 만큼 넓혀 레이어 경계 밖의 바깥 획도 잘리지 않음
//  · 8 / 16 / 32bpc, SmartFX, 멀티프레임 렌더링 지원
//  · AE 이펙트 버퍼는 straight alpha — 합성은 premultiplied 로 누적한 뒤 마지막에 straight 로 되돌린다

#include "BANG_Stroke.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <chrono>

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
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "BANG Stroke v%d.%d\rAlpha-edge distance stroke - BANG_Toolbox\rApply again for a second stroke outside the first.",
        BANG_STROKE_MAJOR, BANG_STROKE_MINOR);
    return PF_Err_NONE;
}

static AEGP_PluginID g_plugin_id = 0;
static bool          g_registered = false;

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(BANG_STROKE_MAJOR, BANG_STROKE_MINOR, BANG_STROKE_BUG, BANG_STROKE_STAGE, BANG_STROKE_BUILD);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER | PF_OutFlag_SEND_UPDATE_PARAMS_UI;
    if (!g_registered && in_data->appl_id != kAppID_Premiere) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        if (suites.UtilitySuite3()->AEGP_RegisterWithAEGP(NULL, "BANG Stroke", &g_plugin_id) == A_Err_NONE) g_registered = true;
    }
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_REVEALS_ZERO_ALPHA |
                           PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;   // 그룹 flags 존중 (하위 그룹은 접힌 채 시작)
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    #define LINE "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"   // ──────
    #define TOPIC_CLOSED(NAME, ID) do { AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_START_COLLAPSED; PF_ADD_TOPIC(NAME, ID); } while (0)
    #define TOPIC_END(ID)          do { AEFX_CLR_STRUCT(def); PF_END_TOPIC(ID); } while (0)
    #define FSLIDER(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, ID) do { AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, 0, ID); } while (0)

    FSLIDER("Width (px)", 0, 1000, 0, 60, 6, PF_Precision_TENTHS, 0, BS_WIDTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Position", 3, BS_POS_OUTSIDE, "Outside|Center|Inside", BS_POSITION);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Corner", 3, BS_CORNER_ROUND, "Round|Miter|Bevel", BS_CORNER);
    FSLIDER("Miter Limit", 1, 10, 1, 10, 4, PF_Precision_TENTHS, 0, BS_MITER_LIMIT);
    FSLIDER("Offset (px)", -500, 500, -20, 20, 0, PF_Precision_TENTHS, 0, BS_OFFSET);
    FSLIDER("Softness (px)", 0, 200, 0, 20, 0, PF_Precision_TENTHS, 0, BS_SOFTNESS);
    FSLIDER("Opacity", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BS_OPACITY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Blend", 4, BS_BLEND_NORMAL, "Normal|Multiply|Screen|Add", BS_BLEND);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Fill", 2, BS_FILL_SOLID, "Solid|Gradient", PF_ParamFlag_SUPERVISE, BS_FILL);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color", PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8, BS_COLOR);

    TOPIC_CLOSED("Gradient " LINE, BS_G_GRAD);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color B", 0, 153, 255, BS_COLOR_B);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Gradient Type", 3, BS_GRAD_ACROSS, "Across Stroke|Linear|Radial", BS_GRAD_TYPE);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    PF_ADD_ANGLE("Gradient Angle", 0, BS_GRAD_ANGLE);
    FSLIDER("Gradient Scale (px)", 1, 10000, 1, 1000, 200, PF_Precision_TENTHS, 0, BS_GRAD_SCALE);
    FSLIDER("Opacity A", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BS_GRAD_OP_A);
    FSLIDER("Opacity B", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BS_GRAD_OP_B);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Reverse", FALSE, 0, BS_GRAD_REV);
    TOPIC_END(BS_G_GRAD_END);

    TOPIC_CLOSED("Edge Noise " LINE, BS_G_NOISE);
    FSLIDER("Amount (px)", 0, 500, 0, 50, 0, PF_Precision_TENTHS, 0, BS_N_AMOUNT);
    FSLIDER("Scale (px)", 1, 2000, 2, 200, 30, PF_Precision_TENTHS, 0, BS_N_SCALE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Detail", 1, 5, 1, 5, 2, BS_N_DETAIL);
    AEFX_CLR_STRUCT(def); def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    PF_ADD_ANGLE("Evolution", 0, BS_N_EVOLUTION);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Seed", 0, 9999, 0, 100, 0, BS_N_SEED);
    TOPIC_END(BS_G_NOISE_END);

    TOPIC_CLOSED("Body " LINE, BS_G_BODY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Body", 2, BS_BODY_KEEP, "Keep|Hide (stroke only)", BS_BODY);
    FSLIDER("Body Opacity", 0, 100, 0, 100, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, BS_BODY_OPACITY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Order", 2, BS_ORDER_BEHIND, "Stroke Behind|Stroke In Front", BS_ORDER);
    TOPIC_END(BS_G_BODY_END);
    #undef FSLIDER
    #undef TOPIC_CLOSED
    #undef TOPIC_END
    #undef LINE

    out_data->num_params = BS_NUM_PARAMS;
    return err;
}

// ── ECW 상태: Fill 이 Solid 면 Gradient 그룹을 회색으로 ─────────
//  주의: 회색처리하는 그룹 안에 ‘다시 켜는 버튼’ 을 두면 안 된다(v1.2 에서 Enable 을 그룹 안에 뒄다가
//  사용자가 Stroke 2/3 을 영영 켜지 못했다). 여기서 Fill 은 그룹 밖에 있으므로 안전하다.
static PF_Err UpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    if (!g_registered) return PF_Err_NONE;
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const bool grad = params[BS_FILL]->u.pd.value == BS_FILL_GRADIENT;
    PF_ParamDef g = *params[BS_G_GRAD];
    g.param_type = PF_Param_GROUP_START;
    if (grad) { g.ui_flags &= ~PF_PUI_DISABLED; g.flags &= ~PF_ParamFlag_COLLAPSE_TWIRLY; }
    else      { g.ui_flags |=  PF_PUI_DISABLED; g.flags |=  PF_ParamFlag_COLLAPSE_TWIRLY; }
    ERR2(suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, BS_G_GRAD, &g));
    return err;
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], const PF_UserChangedParamExtra* extra)
{
    if (extra->param_index == BS_FILL) {
        PF_Err err = UpdateParamsUI(in_data, out_data, params);
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
        return err;
    }
    return PF_Err_NONE;
}

// ── 프리렌더: 파라미터 읽기, 여백 계산, 입력 체크아웃, 출력 영역 확장 ──

static void DeletePreRenderData(void* p) { delete reinterpret_cast<BS_PreRenderData*>(p); }

static PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef pd;

    BS_PreRenderData* d = new BS_PreRenderData();
    memset(d, 0, sizeof(*d));

    // 다운샘플(Draft/해상도) 보정: px 파라미터는 풀해상도 기준
    PF_FpLong dsx = (PF_FpLong)in_data->downsample_x.num / (PF_FpLong)in_data->downsample_x.den;
    PF_FpLong dsy = (PF_FpLong)in_data->downsample_y.num / (PF_FpLong)in_data->downsample_y.den;
    PF_FpLong ds = std::min(dsx, dsy);
    if (ds <= 0) ds = 1;

    #define CHK(idx) AEFX_CLR_STRUCT(pd); ERR(PF_CHECKOUT_PARAM(in_data, idx, in_data->current_time, in_data->time_step, in_data->time_scale, &pd));
    CHK(BS_POSITION);      d->position    = pd.u.pd.value;
    CHK(BS_CORNER);        d->corner      = pd.u.pd.value;
    CHK(BS_MITER_LIMIT);   d->miterLimit  = std::max(1.0, pd.u.fs_d.value);
    CHK(BS_WIDTH);         d->width       = pd.u.fs_d.value * ds;
    CHK(BS_OFFSET);        d->offset      = pd.u.fs_d.value * ds;
    CHK(BS_SOFTNESS);      d->softness    = pd.u.fs_d.value * ds;
    CHK(BS_OPACITY);       d->opacity     = pd.u.fs_d.value / 100.0;
    CHK(BS_BLEND);         d->blend       = pd.u.pd.value;
    CHK(BS_FILL);          d->fill        = pd.u.pd.value;
    CHK(BS_COLOR);         d->colorA      = pd.u.cd.value;
    CHK(BS_COLOR_B);       d->colorB      = pd.u.cd.value;
    CHK(BS_GRAD_TYPE);     d->gradType    = pd.u.pd.value;
    CHK(BS_GRAD_ANGLE);    d->gradAngle   = FIX_2_FLOAT(pd.u.ad.value);
    CHK(BS_GRAD_SCALE);    d->gradScale   = pd.u.fs_d.value * ds;
    CHK(BS_GRAD_OP_A);     d->gradOpA     = pd.u.fs_d.value / 100.0;
    CHK(BS_GRAD_OP_B);     d->gradOpB     = pd.u.fs_d.value / 100.0;
    CHK(BS_GRAD_REV);      d->gradRev     = pd.u.bd.value != 0;
    CHK(BS_N_AMOUNT);      d->noiseAmount = pd.u.fs_d.value * ds;
    CHK(BS_N_SCALE);       d->noiseScale  = std::max(1.0, pd.u.fs_d.value * ds);
    CHK(BS_N_DETAIL);      d->noiseDetail = pd.u.sd.value;
    CHK(BS_N_EVOLUTION);   d->noiseEvo    = FIX_2_FLOAT(pd.u.ad.value);
    CHK(BS_N_SEED);        d->noiseSeed   = pd.u.sd.value;
    CHK(BS_BODY);          d->body        = pd.u.pd.value;
    CHK(BS_BODY_OPACITY);  d->bodyOpacity = pd.u.fs_d.value / 100.0;
    CHK(BS_ORDER);         d->order       = pd.u.pd.value;
    #undef CHK
    if (err) { delete d; return err; }

    // 안쪽·중앙 획은 본체 위에 그려야 보인다(Layer Style 과 동일). 'Order' 는 바깥 획에만 의미가 있음
    d->front = (d->order == BS_ORDER_FRONT) || (d->position != BS_POS_OUTSIDE);

    // 바깥으로 뻗는 최대 거리 = 오프셋 + 두께(위치에 따라) + 부드러움 + 노이즈 + 여유
    PF_FpLong reach = std::max(0.0, d->offset) + d->softness + ((d->position == BS_POS_INSIDE) ? 0.0 : d->width);
    // 획이 실제로 닿는 거리 범위 (모서리 보정을 쓸데없는 쪽 거리장에는 돌리지 않기 위해)
    {
        PF_FpLong half = d->width * 0.5, ctr;
        if (d->position == BS_POS_INSIDE)      ctr = -d->offset - half;
        else if (d->position == BS_POS_CENTER) ctr = d->offset;
        else                                   ctr = d->offset + half;
        d->bandLo = ctr - half - d->softness - d->noiseAmount;
        d->bandHi = ctr + half + d->softness + d->noiseAmount;
    }

    // 마이터는 모서리가 한계 배까지 뻗는다 — 다만 두꺼운 획에서 격자가 폭발하지 않게 +1000px 로 상한
    if (d->corner == BS_CORNER_MITER) reach = std::min(reach * d->miterLimit, reach + 1000.0);
    d->margin = (A_long)std::ceil(reach + d->noiseAmount + 2.0);
    LOGF("PreRender pos=%ld width=%.1f offset=%.1f soft=%.1f noise=%.1f margin=%ld", d->position, d->width, d->offset, d->softness, d->noiseAmount, d->margin);

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
//  argOut 이 널이 아니면 각 위치의 최소값을 만든 씨앗 인덱스도 돌려준다 (모서리 처리용)
static void edt1d(const float* f, float* dOut, int* argOut, int n, std::vector<int>& v, std::vector<float>& z)
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
        if (argOut) argOut[q] = vk;
    }
}

// grid: 0 = 씨앗(거리 0), INF = 나머지. 결과: 씨앗까지의 제곱 거리
//  site 가 널이 아니면 각 픽셀의 최근접 씨앗 픽셀 인덱스(y*w+x)도 채운다
static void edt2d(std::vector<float>& g, int w, int h, std::vector<int>* site = nullptr)
{
    std::vector<float> col(std::max(w, h)), out(std::max(w, h));
    std::vector<int> v; std::vector<float> z;
    std::vector<int> arg, colArg;
    if (site) { arg.resize(std::max(w, h)); colArg.resize((size_t)w * h); site->resize((size_t)w * h); }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) col[y] = g[(size_t)y * w + x];
        edt1d(col.data(), out.data(), site ? arg.data() : nullptr, h, v, z);
        for (int y = 0; y < h; y++) {
            g[(size_t)y * w + x] = out[y];
            if (site) colArg[(size_t)y * w + x] = arg[y];     // 이 열에서 가장 가까운 씨앗의 행
        }
    }
    for (int y = 0; y < h; y++) {
        edt1d(&g[(size_t)y * w], out.data(), site ? arg.data() : nullptr, w, v, z);
        if (site) {
            for (int x = 0; x < w; x++) {
                int ax = arg[x];                              // 이 행에서 고른 열
                (*site)[(size_t)y * w + x] = colArg[(size_t)y * w + ax] * w + ax;
            }
        }
        memcpy(&g[(size_t)y * w], out.data(), (size_t)w * sizeof(float));
    }
}

// ── 모서리 각지게 (Miter / Bevel) ────────────────────────────
//  이진 마스크의 계단 때문에 "최근접 씨앗의 부채꼴"만으로는 회전한 도형의 꼭짓점 각도를 못 맞춘다
//  (0°/45° 는 맞고 10°/22.5° 는 과하거나 모자람). 그래서 **안티에일리어싱된 알파의 기울기**로
//  경계 픽셀마다 바깥 법선 n 과 0.5 등고선까지의 거리 t 를 구한다 — 회전에 무관하게 정확하다.
//    · corner 픽셀 = 주변 법선이 크게 벌어지는 곳(볼록 꼭짓점). 최근접 씨앗이 corner 근처일 때만 손댄다
//      → 직선 구간(두께 유지)과 오목한 모서리(원래 각짐)는 건드리지 않는다.
//    · Miter: d = max over 꼭짓점 주변 '깨끗한' 변의 지지 평면 거리 (둥근 거리보다 작아 모서리가 뻗는다)
//    · Bevel: 양 끝 법선 n1, n2 의 이등분 평면을 더해 꼭짓점을 잘라낸다. Miter Limit 초과 시에도 동일.
//  성능: 평면을 **꼭짓점마다 한 번** 모아 법선이 같은 것끼리 합쳐 두고(보통 2~4개), 띠 픽셀은
//  그 몇 개만 계산한다. 예전처럼 픽셀마다 17×17 창을 두 번 훑으면 텍스트 한 장에 170ms 가 든다.
//  dist 는 실제 거리(in-place), maxReach 밖은 손대지 않는다. sgn = +1 바깥 거리장 / −1 안쪽 거리장.
static void SharpenCorners(std::vector<float>& dist, const std::vector<int>& site, const std::vector<float>& alpha,
                           int w, int h, A_long mode, float limit, float maxReach, float sgn,
                           int bx0, int by0, int bx1, int by1)
{
    const size_t N = (size_t)w * h;
    if (site.size() != N || alpha.size() != N) return;
    const float RMIN = 1.2f;
    const float MAG_MIN = 0.12f;        // 이보다 완만하면 법선을 믿을 수 없다
    const float T_MAX = 1.5f;           // 0.5 등고선이 1.5px 넘게 떨어져 있으면 경계 픽셀이 아니다
    const float CORNER_COS = 0.70f;     // 주변 법선이 45° 넘게 벌어지면 꼭지점 (30° 기준이면 반지름 3~4px 곱선도 꼭지점으로 잡혔다)
    const int   R = 8;                  // 지지 평면을 모을 반경 (꼭짓점 주변 3px 는 제외되므로 넓게)
    const int   CR = 2;                 // 꼭짓점 판정 반경
    const int   DIL = 3;                // 무딘 꼭짓점 주변 제외 반경 = 최근접 씨앗 허용 반경
    const int   MAXPL = 8;              // 꼭짓점당 보관할 평면 수

    // 알파가 0 이 아닌 구역(= 레이어 내용 + 여유)만 훑는다. 격자는 그보다 훨씬 넓다.
    bx0 = std::max(1, bx0 - 4); by0 = std::max(1, by0 - 4);
    // 법선 계산이 i±1 · i±w 를 읽으므로 마지막 행/열은 반드시 제외 (여기서 h-1 을 쓰면 버퍼 밖을 읽는다)
    bx1 = std::min(w - 2, bx1 + 4); by1 = std::min(h - 2, by1 + 4);
    if (bx1 <= bx0 || by1 <= by0) return;

    // 1픽셀 차분은 거의 수평/수직인 변에서 법선을 축에 딱 붙게 양자화한다(10° 기울기가 0° 로 보임)
    // → [1 4 6 4 1]/16 로 한 번 부드럽게 만든 알파에서 기울기를 재다. 회전각과 무관하게 정확해진다.
    const auto _s0 = std::chrono::steady_clock::now();
    // 읽는 곳은 전부 먼저 쓰므로 0 초기화가 필요 없다 (큰 격자에서 memset 만 수 ms)
    std::unique_ptr<float[]> smBuf(new float[N]), tmpBuf(new float[N]);
    float* sm = smBuf.get(); float* tmp = tmpBuf.get();
    {
        const float k[5] = { 1.f / 16, 4.f / 16, 6.f / 16, 4.f / 16, 1.f / 16 };
        for (int y = by0 - 2; y <= by1 + 2; y++) {
            if (y < 0 || y >= h) continue;
            for (int x = bx0 - 2; x <= bx1 + 2; x++) {
                if (x < 0 || x >= w) continue;
                float a = 0;
                for (int i = 0; i < 5; i++) { const int xx = std::min(w - 1, std::max(0, x - 2 + i)); a += k[i] * alpha[(size_t)y * w + xx]; }
                tmp[(size_t)y * w + x] = a;
            }
        }
        for (int y = by0; y <= by1; y++) for (int x = bx0; x <= bx1; x++) {
            float a = 0;
            for (int i = 0; i < 5; i++) { const int yy = std::min(h - 1, std::max(0, y - 2 + i)); a += k[i] * tmp[(size_t)yy * w + x]; }
            sm[(size_t)y * w + x] = a;
        }
    }

    std::unique_ptr<float[]> nxBuf(new float[N]), nyBuf(new float[N]), ttBuf(new float[N]);
    float* nx = nxBuf.get(); float* ny = nyBuf.get(); float* tt = ttBuf.get();   // flag != 0 인 곳만 읽는다
    std::vector<unsigned char> flag(N, 0);      // 0 없음 · 1 깨끗한 변 · 2 꼭짓점 · 3 꼭짓점 주변(평면 출처에서 제외)
    for (int y = by0; y <= by1; y++) for (int x = bx0; x <= bx1; x++) {
        const size_t i = (size_t)y * w + x;
        const float gx = (sm[i + 1] - sm[i - 1]) * 0.5f;
        const float gy = (sm[i + w] - sm[i - w]) * 0.5f;
        const float mag = std::sqrt(gx * gx + gy * gy);
        if (mag < MAG_MIN) continue;
        const float t = sgn * (sm[i] - 0.5f) / mag;             // 픽셀 중심 → 0.5 등고선 (법선 방향 부호 거리)
        if (t > T_MAX || t < -T_MAX) continue;
        nx[i] = -sgn * gx / mag; ny[i] = -sgn * gy / mag;       // 바깥(= 알파가 줄어드는) 방향
        tt[i] = t;
        flag[i] = 1;
    }
    std::vector<int> edgePix;                                   // 경계 픽셀만 추려 이후 패스를 가볍게
    edgePix.reserve(4096);
    for (int y = by0; y <= by1; y++) for (int x = bx0; x <= bx1; x++)
        if (flag[(size_t)y * w + x]) edgePix.push_back(y * w + x);

    for (size_t e = 0; e < edgePix.size(); e++) {
        const int i = edgePix[e];
        const int x = i % w, y = i / w;
        float worst = 1.f;
        for (int dy = -CR; dy <= CR; dy++) {
            const int yy = y + dy; if (yy < 0 || yy >= h) continue;
            for (int dx = -CR; dx <= CR; dx++) {
                const int xx = x + dx; if (xx < 0 || xx >= w) continue;
                const size_t j = (size_t)yy * w + xx;
                if (!flag[j]) continue;
                const float c = nx[i] * nx[j] + ny[i] * ny[j];
                if (c < worst) worst = c;
            }
        }
        if (worst < CORNER_COS) flag[i] = 2;
    }
    // 무딜어진 꼭짓점 주변의 법선은 이등분선 쪽으로 기울어 마이터를 뭉툭하게 만든다 → 평면 출처에서 제외(3)
    // 동시에 '이 경계 픽셀에서 DIL 안에 있는 꼭짓점' 을 기록해 두면 띠 픽셀은 조회 한 번으로 끝난다.
    std::vector<int> cornerOf(N, -1);
    std::vector<int> cornerPix;
    {
        std::vector<unsigned char> f2 = flag;
        for (size_t e = 0; e < edgePix.size(); e++) {
            const int i = edgePix[e];
            const int x = i % w, y = i / w;
            int found = -1;
            for (int dy = -DIL; dy <= DIL && found < 0; dy++) {
                const int yy = y + dy; if (yy < 0 || yy >= h) continue;
                for (int dx = -DIL; dx <= DIL; dx++) {
                    const int xx = x + dx; if (xx < 0 || xx >= w) continue;
                    if (flag[(size_t)yy * w + xx] == 2) { found = yy * w + xx; break; }
                }
            }
            if (found >= 0) {
                cornerOf[i] = found;
                if (flag[i] == 1) f2[i] = 3;
            }
        }
        flag.swap(f2);
        for (size_t e = 0; e < edgePix.size(); e++) if (flag[edgePix[e]] == 2) cornerPix.push_back(edgePix[e]);
    }
    if (cornerPix.empty()) return;

    // 꼭짓점마다 주변 '깨끗한 변' 평면을 한 번만 모아 법선이 같은 것끼리 합친다 (보통 2~4개)
    // s(p) = n·p − c. 같은 변에서 나온 평면은 **평균**을 낸다 — 가장 바깥을 고르면 AA 잡음만큼
    // 평면이 밀려 모서리가 1~2px 과하게 뻗는다.
    struct Plane { float nx, ny, c; float sx, sy, sc; int n; };
    std::vector<int> planeStart(cornerPix.size() + 1, 0);
    std::vector<Plane> planes;
    std::vector<float> apexX(cornerPix.size()), apexY(cornerPix.size());
    std::unique_ptr<int[]> cornerIdxBuf(new int[N]);
    int* cornerIdx = cornerIdxBuf.get();        // 꼭짓점 픽셀만 쓰고 그 자리만 읽는다
    planes.reserve(cornerPix.size() * 4);
    for (size_t ci = 0; ci < cornerPix.size(); ci++) {
        const int cp = cornerPix[ci];
        const int cx = cp % w, cy = cp / w;
        cornerIdx[cp] = (int)ci;
        apexX[ci] = (float)cx + nx[cp] * tt[cp];
        apexY[ci] = (float)cy + ny[cp] * tt[cp];
        planeStart[ci] = (int)planes.size();
        for (int dy = -R; dy <= R; dy++) {
            const int yy = cy + dy; if (yy < 0 || yy >= h) continue;
            for (int dx = -R; dx <= R; dx++) {
                const int xx = cx + dx; if (xx < 0 || xx >= w) continue;
                const size_t j = (size_t)yy * w + xx;
                if (flag[j] != 1) continue;
                const float c = nx[j] * ((float)xx + nx[j] * tt[j]) + ny[j] * ((float)yy + ny[j] * tt[j]);
                bool merged = false;
                for (int q = planeStart[ci]; q < (int)planes.size(); q++) {
                    if (planes[q].nx * nx[j] + planes[q].ny * ny[j] > 0.98f) {   // 같은 변
                        planes[q].sx += nx[j]; planes[q].sy += ny[j]; planes[q].sc += c; planes[q].n++;
                        merged = true; break;
                    }
                }
                if (!merged && (int)planes.size() - planeStart[ci] < MAXPL)
                    planes.push_back({ nx[j], ny[j], c, nx[j], ny[j], c, 1 });
            }
        }
    }
    planeStart[cornerPix.size()] = (int)planes.size();
    for (size_t q = 0; q < planes.size(); q++) {            // 누적값 → 평균 평면
        Plane& P = planes[q];
        const float l = std::sqrt(P.sx * P.sx + P.sy * P.sy);
        if (P.n > 0 && l > 1e-6f) { P.nx = P.sx / l; P.ny = P.sy / l; P.c = P.sc / P.n; }
    }

    const auto _s1 = std::chrono::steady_clock::now();
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const size_t i = (size_t)y * w + x;
        const float r = dist[i];
        if (r < RMIN || r > maxReach) continue;
        const int s = site[i];
        if (s < 0 || (size_t)s >= N) continue;
        const int cp = cornerOf[s];                 // 최근접 씨앗 근처의 꼭짓점 (없으면 직선 구간)
        if (cp < 0) continue;
        const int ci = cornerIdx[cp];
        if (ci < 0) continue;
        const int p0 = planeStart[ci], p1 = planeStart[ci + 1];
        if (p1 <= p0) continue;
        // EDT 거리는 ‘씨앗 픽셀 중심’ 기준, 지지 평면은 ‘0.5 등고선(진짜 경계)’ 기준이라 0.5px 차이가 난다.
        // 이걸 맞추지 않으면 보정된 구간과 그렇지 않은 구간의 경계가 1px 씩 엇갈려 외곽이 지저분해진다.
        const float rT = r - 0.5f;                  // 경계까지의 실제 거리
        float best = -1e30f, b1x = 0, b1y = 0;
        for (int q = p0; q < p1; q++) {
            const float sb = planes[q].nx * (float)x + planes[q].ny * (float)y - planes[q].c;
            if (sb > best) { best = sb; b1x = planes[q].nx; b1y = planes[q].ny; }
        }
        // 지지 평면 거리가 둘렉거리보다 크면 볼록한 꼭지점이 아니다(오목한 모서리) → 그대로 둔다
        if (best > rT + 0.5f) continue;
        float best2 = -1e30f, b2x = 0, b2y = 0;
        for (int q = p0; q < p1; q++) {
            if (planes[q].nx * b1x + planes[q].ny * b1y > 0.94f) continue;
            const float sb = planes[q].nx * (float)x + planes[q].ny * (float)y - planes[q].c;
            if (sb > best2) { best2 = sb; b2x = planes[q].nx; b2y = planes[q].ny; }
        }
        // 두 번째 변을 못 찾았으면 꼭지점이라 볼 근거가 없다 — 한 평면만으로 당기면 제한 없는 뿔이 생긴다
        if (best2 <= -1e29f) continue;
        const float cosFull = std::min(1.f, std::max(-1.f, b1x * b2x + b1y * b2y));
        const float cpsi = std::sqrt(std::max(0.f, (1.f + cosFull) * 0.5f));     // cos(두 법선 사이 각 / 2)
        // 보정은 **꼭지점의 부채꼴 안**에서만 한다. 부채꼴 밖(= 그냥 변 옆)은 둘렉거리가 이미 정답이고,
        // 거기까지 평면 값으로 덮어쓰면 평면 오차만큼 경계가 어긋나 외곽에 1px 계단이 생긴다.
        // 부채꼴 경계에서는 두 값이 일치하므로 이음새가 없다.
        float bsx = b1x + b2x, bsy = b1y + b2y;
        const float bsl = std::sqrt(bsx * bsx + bsy * bsy);
        if (bsl < 1e-6f) continue;
        bsx /= bsl; bsy /= bsl;
        const float vx = (float)x - apexX[ci], vy = (float)y - apexY[ci];
        const float vlen = std::sqrt(vx * vx + vy * vy);
        if (vlen < 1e-3f) continue;
        if ((vx * bsx + vy * bsy) / vlen < cpsi - 0.02f) continue;      // 부채꼴 밖

        const float maxOut = rT / std::max(limit, 1.f);     // Miter Limit 을 넘는 뻗음은 어떤 경우도 허용하지 않는다
        float dm = std::max(std::min(best, rT), maxOut);
        if (mode == BS_CORNER_BEVEL || cpsi < 1e-4f || 1.f / std::max(cpsi, 1e-4f) > limit) {
            // 너무 날카로운 각은 꼭지점 추정 오차가 1/cpsi 로 증폭되므로 현 평면 대신 한계치로 자른다
            if (cpsi < 0.25f) {
                dm = std::max(dm, maxOut);
            } else {
                const float db = (bsx * vx + bsy * vy) / cpsi;
                dm = std::max(dm, std::min(db, rT * 1.5f));      // 꼭지점을 잘라도 과도하게 파고들지는 않게
            }
        }
        dist[i] = std::max(0.f, dm + 0.5f);         // 다시 EDT 기준으로
    }
    LOGF("  Sharpen setup %.1f ms | scan %.1f ms | corners=%d planes=%d",
         std::chrono::duration<double, std::milli>(_s1 - _s0).count(),
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _s1).count(),
         (int)cornerPix.size(), (int)planes.size());
}

// ── 값 노이즈 (fBm) — 가장자리 거칠게 ──
static float HashNoise(int x, int y, int z, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du ^ (seed + 1u) * 0x27D4EB2Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 8388607.5f - 1.0f;   // [-1, 1]
}
static float Smooth(float t) { return t * t * (3.f - 2.f * t); }
static float ValueNoise2(float x, float y, int z, uint32_t seed)
{
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float tx = Smooth(x - xi), ty = Smooth(y - yi);
    float n00 = HashNoise(xi, yi, z, seed), n10 = HashNoise(xi + 1, yi, z, seed);
    float n01 = HashNoise(xi, yi + 1, z, seed), n11 = HashNoise(xi + 1, yi + 1, z, seed);
    return (n00 * (1 - tx) + n10 * tx) * (1 - ty) + (n01 * (1 - tx) + n11 * tx) * ty;
}
// Evolution(회전각)을 3번째 축으로 써서 부드럽게 흐르게 한다 (60° = 노이즈 한 칸)
static float FbmNoise(float x, float y, float evo, int octaves, uint32_t seed)
{
    float zf = evo / 60.0f;
    int z0 = (int)std::floor(zf); float tz = Smooth(zf - z0);
    float sum = 0, amp = 1, norm = 0, fx = x, fy = y;
    for (int o = 0; o < octaves; o++) {
        float a = ValueNoise2(fx, fy, z0 + o * 31, seed), b = ValueNoise2(fx, fy, z0 + 1 + o * 31, seed);
        sum += (a * (1 - tz) + b * tz) * amp;
        norm += amp; amp *= 0.5f; fx *= 2.03f; fy *= 2.01f;
    }
    // 값 노이즈 fBm 은 ±1 을 거의 못 채우므로 약간 키워 Amount(px) 와 체감을 맞춘다
    return (norm > 0) ? std::min(std::max(sum / norm * 1.7f, -1.5f), 1.5f) : 0.f;
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
    // 격자 = 출력 영역 ∪ 입력 영역 + 2px.
    //  씨앗(전경 픽셀)은 전부 입력 영역 안에 있고 그 밖은 어차피 투명이므로, 예전처럼 출력 영역을
    //  여백(margin)만큼 사방으로 넓힐 필요가 없다. 마이터는 여백이 Limit 배라 격자가 2.5배까지 커졌다.
    const int pad = 2;
    const int ux0 = std::min((int)c.out_rect.left, (int)c.d->in_rect.left);
    const int uy0 = std::min((int)c.out_rect.top, (int)c.d->in_rect.top);
    const int ux1 = std::max((int)c.out_rect.right, (int)c.d->in_rect.right);
    const int uy1 = std::max((int)c.out_rect.bottom, (int)c.d->in_rect.bottom);
    c.gx0 = ux0 - pad; c.gy0 = uy0 - pad;
    c.gw = (ux1 - ux0) + pad * 2;
    c.gh = (uy1 - uy0) + pad * 2;
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
    const auto _t0 = std::chrono::steady_clock::now();
    // 획이 닿지 않는 쪽 거리장은 EDT 자체를 생략한다. 경계 픽셀(알파 커버리지로 보정되는 1px)만
    // 거리 1 로 표시해 두면 아래 합성 규칙이 그대로 동작하고, 나머지는 ‘아주 멀’ 으로 두면 된다.
    const bool needIn = (c.d->bandLo < -1.0), needOut = (c.d->bandHi > 1.0);
    const float VERY_FAR = 1e6f;   // FAR / near 는 windows.h 매크로라 쓸 수 없다
    if (!needIn || !needOut) {
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            const size_t i = (size_t)y * w + x;
            const bool fg = c.alpha[i] >= 0.5f;
            bool edge = false;
            if (x > 0 && (c.alpha[i - 1] >= 0.5f) != fg) edge = true;
            else if (x < w - 1 && (c.alpha[i + 1] >= 0.5f) != fg) edge = true;
            else if (y > 0 && (c.alpha[i - w] >= 0.5f) != fg) edge = true;
            else if (y < h - 1 && (c.alpha[i + w] >= 0.5f) != fg) edge = true;
            if (!needIn)  inside[i]  = fg ? (edge ? 1.f : VERY_FAR) : 0.f;
            if (!needOut) outside[i] = fg ? 0.f : (edge ? 1.f : VERY_FAR);
        }
    }
    if (c.d->corner == BS_CORNER_ROUND) {
        if (needIn)  { edt2d(inside, w, h);  for (size_t i = 0; i < (size_t)w * h; i++) inside[i]  = std::sqrt(inside[i]); }
        if (needOut) { edt2d(outside, w, h); for (size_t i = 0; i < (size_t)w * h; i++) outside[i] = std::sqrt(outside[i]); }
    } else {
        // 모서리를 각지게: 최근접 씨앗을 함께 구한 뒤 알파 기울기로 복원한 변의 법선으로 Miter/Bevel 거리를 다시 쓴다
        //  · 획이 닿지 않는 쪽 거리장은 아예 건드리지 않는다 (Outside 면 안쪽, Inside 면 바깥쪽)
        //  · 법선·꼭짓점 계산 범위는 레이어 내용 상자로 제한 (격자는 여백 때문에 그보다 훨씬 넓다)
        const float lim = (float)c.d->miterLimit;
        const int cbx0 = c.d->in_rect.left - c.gx0, cby0 = c.d->in_rect.top - c.gy0;
        const int cbx1 = c.d->in_rect.right - c.gx0, cby1 = c.d->in_rect.bottom - c.gy0;
        std::vector<int> site;
        if (needIn) {
            edt2d(inside, w, h, &site);
            for (size_t i = 0; i < (size_t)w * h; i++) inside[i] = std::sqrt(inside[i]);
            SharpenCorners(inside, site, c.alpha, w, h, c.d->corner, lim,
                           (float)((-c.d->bandLo + 2.0) * lim + 2.0), -1.f, cbx0, cby0, cbx1, cby1);
        }
        if (needOut) {
            edt2d(outside, w, h, &site);
            for (size_t i = 0; i < (size_t)w * h; i++) outside[i] = std::sqrt(outside[i]);
            SharpenCorners(outside, site, c.alpha, w, h, c.d->corner, lim,
                           (float)((c.d->bandHi + 2.0) * lim + 2.0), 1.f, cbx0, cby0, cbx1, cby1);
        }
    }
    LOGF("BuildSDF %dx%d corner=%ld : %.1f ms", w, h, (long)c.d->corner,
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _t0).count());
    c.sdf.resize((size_t)w * h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        float a = c.alpha[i];
        float dIn = inside[i], dOut = outside[i];
        // 전경 픽셀은 배경까지 거리(dIn), 배경 픽셀은 전경까지 거리(dOut)만 유효 (자기 자신은 항상 씨앗이라 0)
        bool fg = (a >= 0.5f);
        float d = fg ? -(dIn - 0.5f) : (dOut - 0.5f);
        if ((fg && dIn <= 1.0f) || (!fg && dOut <= 1.0f)) d = 0.5f - a;   // 경계 픽셀은 알파 커버리지로 서브픽셀 보정
        c.sdf[i] = d;
    }
}

static inline void BlendRGB(A_long mode, float br_, float bg_, float bb_, float& r, float& g, float& b)
{
    switch (mode) {
        case BS_BLEND_MULTIPLY: r *= br_; g *= bg_; b *= bb_; break;
        case BS_BLEND_SCREEN:   r = 1 - (1 - r) * (1 - br_); g = 1 - (1 - g) * (1 - bg_); b = 1 - (1 - b) * (1 - bb_); break;
        case BS_BLEND_ADD:      r = std::min(1.f, r + br_); g = std::min(1.f, g + bg_); b = std::min(1.f, b + bb_); break;
        default: break;   // Normal
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
    const float op = (float)d->opacity;
    const float ar = Chan<PF_Pixel>::get(d->colorA.red), ag = Chan<PF_Pixel>::get(d->colorA.green), ab = Chan<PF_Pixel>::get(d->colorA.blue);
    const float br = Chan<PF_Pixel>::get(d->colorB.red), bg = Chan<PF_Pixel>::get(d->colorB.green), bb = Chan<PF_Pixel>::get(d->colorB.blue);
    const float gang = (float)(d->gradAngle * 3.14159265358979 / 180.0);
    const float gcos = std::cos(gang), gsin = std::sin(gang), gscale = (float)std::max(1.0, d->gradScale);
    const float gopA = (float)d->gradOpA, gopB = (float)d->gradOpB;
    const float ccx = (float)(d->in_rect.left + d->in_rect.right) * 0.5f;    // 내용 중심 (그라데이션 기준)
    const float ccy = (float)(d->in_rect.top + d->in_rect.bottom) * 0.5f;
    const bool hideBody = (d->body == BS_BODY_HIDE);
    const float bodyOp = (float)d->bodyOpacity;
    const bool front = d->front;
    const float noiseAmt = (float)d->noiseAmount, noiseScale = (float)d->noiseScale, noiseEvo = (float)d->noiseEvo;
    const int noiseOct = std::min(std::max((int)d->noiseDetail, 1), 5);
    const uint32_t noiseSeed = (uint32_t)d->noiseSeed;

    for (int y = 0; y < oh; y++) {
        P* orow = (P*)((char*)c.out->data + y * c.out->rowbytes);
        int iy = y + offy;
        const P* irow = (iy >= 0 && iy < ih) ? (const P*)((const char*)c.in->data + iy * c.in->rowbytes) : nullptr;
        const float* srow = &c.sdf[(size_t)(y + goy) * c.gw + gox];
        const float ly = (float)(c.out_rect.top + y) + 0.5f;
        for (int x = 0; x < ow; x++) {
            int ix = x + offx;
            float ba = 0, bodyR = 0, bodyG = 0, bodyB = 0;               // 본체 (straight)
            if (irow && ix >= 0 && ix < iw) {
                const P& p = irow[ix];
                ba = Chan<P>::get(p.alpha); bodyR = Chan<P>::get(p.red); bodyG = Chan<P>::get(p.green); bodyB = Chan<P>::get(p.blue);
            }
            if (hideBody) ba = 0; else ba *= bodyOp;
            const float lx = (float)(c.out_rect.left + x) + 0.5f;
            float dist = srow[x];
            if (noiseAmt > 0.f) dist += FbmNoise(lx / noiseScale, ly / noiseScale, noiseEvo, noiseOct, noiseSeed) * noiseAmt;

            // 획 커버리지: t = half - |dist - center| (px). 0.5px AA + 부드러움
            float t = half - std::fabs(dist - center);
            float cov = (t + 0.5f + soft * 0.5f) / (1.0f + soft);
            cov = std::min(std::max(cov, 0.f), 1.f);
            float sa = cov * op;

            float sr = ar, sg = ag, sb = ab;
            if (sa > 0.f && d->fill == BS_FILL_GRADIENT) {
                float u;
                if (d->gradType == BS_GRAD_LINEAR)      u = 0.5f + ((lx - ccx) * gcos + (ly - ccy) * gsin) / gscale;
                else if (d->gradType == BS_GRAD_RADIAL) u = std::sqrt((lx - ccx) * (lx - ccx) + (ly - ccy) * (ly - ccy)) / gscale;
                else                                    u = (half > 0) ? (dist - (center - half)) / (2.f * half) : 0.f;   // Across Stroke
                u = std::min(std::max(u, 0.f), 1.f);
                if (d->gradRev) u = 1.f - u;
                sr = ar + (br - ar) * u; sg = ag + (bg - ag) * u; sb = ab + (bb - ab) * u;
                sa *= gopA + (gopB - gopA) * u;                                  // 그라데이션 불투명도
            }

            // premultiplied 누적: (뒤) 획 → 본체 → (앞) 획
            float oa = 0, orr = 0, og = 0, ob = 0;
            auto putStroke = [&]() {
                float r = sr, g = sg, b = sb;
                if (d->blend != BS_BLEND_NORMAL && oa > 0.f) BlendRGB(d->blend, orr / oa, og / oa, ob / oa, r, g, b);
                const float k = 1.f - sa;
                oa = sa + oa * k; orr = r * sa + orr * k; og = g * sa + og * k; ob = b * sa + ob * k;
            };
            if (sa > 0.f && !front) putStroke();
            if (ba > 0.f) { const float k = 1.f - ba; oa = ba + oa * k; orr = bodyR * ba + orr * k; og = bodyG * ba + og * k; ob = bodyB * ba + ob * k; }
            if (sa > 0.f && front) putStroke();

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
            case PF_Cmd_UPDATE_PARAMS_UI: err = UpdateParamsUI(in_data, out_data, params); break;
            case PF_Cmd_USER_CHANGED_PARAM: err = UserChangedParam(in_data, out_data, params, (const PF_UserChangedParamExtra*)extra); break;
            case PF_Cmd_SMART_PRE_RENDER: err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
            case PF_Cmd_SMART_RENDER:     err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
            default: break;
        }
    } catch (PF_Err& thrown) { err = thrown; LOGF("EXC cmd=%d err=%d", (int)cmd, (int)err); }
    catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; LOGF("EXC cmd=%d (unknown)", (int)cmd); }
    if (err) LOGF("cmd=%d err=%d", (int)cmd, (int)err);
    return err;
}
