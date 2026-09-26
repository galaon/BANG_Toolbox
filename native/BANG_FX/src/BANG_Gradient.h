// BANG_Gradient.h — BANG Gradient : 다중 색상 정지점 그라데이션 (SmartFX, 8/16/32 bpc)
//
//  AE 기본 Ramp 는 색이 둘뿐이고 sRGB 로만 섞이며, 띠(banding)를 Ramp Scatter 로만 가린다.
//  상용 플러그인(proGradient 등)이 채우고 있는 빈틈을 정리해 담았다:
//    · 정지점 최대 8개 (색 · 위치 · 불투명도 각각)
//    · 모양: Linear / Radial / Angular / Diamond / Reflected / Contour(알파 경계에서의 거리)
//    · 보간 색공간: sRGB / Linear / OKLab(밝기 꺼짐 없음) / OKLCh 색상환(짧은 길·긴 길)
//    · 반복: Clamp / Repeat / Mirror + Phase, Reverse
//    · Dither: 8bpc 로 내보낼 때 생기는 띠를 없애는 미세 노이즈
//    · 원본과 합성: 블렌드 모드 + 양, Preserve Alpha(레이어 알파 안에서만)
#pragma once

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectUI.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include "AE_EffectSuitesHelper.h"

#define BANG_GRAD_MAJOR   1
#define BANG_GRAD_MINOR   5
#define BANG_GRAD_BUG     0
#define BANG_GRAD_STAGE   PF_Stage_DEVELOP
#define BANG_GRAD_BUILD   1

#define BG_NUM_STOPS 8

// 파라미터 인덱스 (0 = 입력 레이어)
enum {
    BG_INPUT = 0,
    BG_SHAPE,           // Shape: Linear | Radial | Angular | Diamond | Reflected | Contour
    BG_START,           // Start (점)
    BG_END,             // End (점)
    BG_FIT_H,           // Fit Horizontal — 내용의 좌·우 가운데로
    BG_FIT_V,           // Fit Vertical   — 내용의 최상단·최하단 가운데로 (누를 때마다 상↔하 반전)
    BG_FIT_D,           // Fit Diagonal   — 누를 때마다 사분면을 시계방향으로 (↘ ↙ ↖ ↗)
    BG_LOCK,            // Lock Gradient  — 표현식으로 레이어에 고정(크기·위치 변화를 추종)
    BG_ANGLE_OFF,       // Angle Offset (Angular)
    BG_CONTOUR_SPAN,    // Contour Span (px)
    BG_REPEAT,          // Repeat: Clamp | Repeat | Mirror
    BG_CYCLES,          // Cycles (반복 횟수)
    BG_PHASE,           // Phase (%)
    BG_REVERSE,         // Reverse

    BG_INTERP,          // Interpolate: sRGB | Linear | OKLab | OKLCh Short | OKLCh Long
    BG_SMOOTH,          // Smoothness (%) — 정지점 사이 이징
    BG_G_STOPS,         // ── Gradient Colors ──
    BG_BAR,             //   가로 색 띄 (커스텀 UI) — 미리보기 · 칩 클릭=색 · 더블클릭=정지점 추가 · ⇄ =좌우 반전
    BG_COUNT,           //   Stops (2~8)
    BG_PRESET,          //   Preset (자주 쓰는 그라데이션)
    BG_RANDOM,          //   Randomize (OKLCh 색상환에서 골라 그럴듯한 조합을 만든다)
    BG_IMPORT,          //   Import… (.css / .ggr / .json)
    BG_EXPORT,          //   Export… (.css / .ggr / .json)
    //   정지점은 하나씩 접히는 그룹이다 — 열려 있으면 24줄이라 Stops 변화마다 아래 버튼이 크게 밀린다
    BG_S1_GRP, BG_S1_COLOR, BG_S1_POS, BG_S1_OP, BG_S1_GRP_END,
    BG_S2_GRP, BG_S2_COLOR, BG_S2_POS, BG_S2_OP, BG_S2_GRP_END,
    BG_S3_GRP, BG_S3_COLOR, BG_S3_POS, BG_S3_OP, BG_S3_GRP_END,
    BG_S4_GRP, BG_S4_COLOR, BG_S4_POS, BG_S4_OP, BG_S4_GRP_END,
    BG_S5_GRP, BG_S5_COLOR, BG_S5_POS, BG_S5_OP, BG_S5_GRP_END,
    BG_S6_GRP, BG_S6_COLOR, BG_S6_POS, BG_S6_OP, BG_S6_GRP_END,
    BG_S7_GRP, BG_S7_COLOR, BG_S7_POS, BG_S7_OP, BG_S7_GRP_END,
    BG_S8_GRP, BG_S8_COLOR, BG_S8_POS, BG_S8_OP, BG_S8_GRP_END,
    BG_G_STOPS_END,

    BG_G_OUT,           // ── Output ──
    BG_ALPHA_MODE,      // Alpha: Composite(원본 위에) | Replace(불투명도가 레이어 알파를 그대로 뚚는다)
    BG_DITHER,          // Dither (%)
    BG_BLEND,           // Blend With Original: Normal | Multiply | Screen | Add | Overlay
    BG_AMOUNT,          // Amount (%)
    BG_PRESERVE_ALPHA,  // Preserve Alpha
    BG_G_OUT_END,

    BG_NUM_PARAMS
};

// i 번째 정지점의 파라미터 인덱스 (0-based)
#define BG_STOP_STRIDE  (BG_S2_GRP - BG_S1_GRP)
#define BG_SG(i)   (BG_S1_GRP     + (i) * BG_STOP_STRIDE)
#define BG_SC(i)   (BG_S1_COLOR   + (i) * BG_STOP_STRIDE)
#define BG_SP(i)   (BG_S1_POS     + (i) * BG_STOP_STRIDE)
#define BG_SO(i)   (BG_S1_OP      + (i) * BG_STOP_STRIDE)
#define BG_SGE(i)  (BG_S1_GRP_END + (i) * BG_STOP_STRIDE)

enum { BG_SHAPE_LINEAR = 1, BG_SHAPE_RADIAL, BG_SHAPE_ANGULAR, BG_SHAPE_DIAMOND, BG_SHAPE_REFLECT, BG_SHAPE_CONTOUR };
enum { BG_REPEAT_CLAMP = 1, BG_REPEAT_REPEAT, BG_REPEAT_MIRROR };
enum { BG_INTERP_SRGB = 1, BG_INTERP_LINEAR, BG_INTERP_OKLAB, BG_INTERP_OKLCH_SHORT, BG_INTERP_OKLCH_LONG };
enum { BG_BLEND_NORMAL = 1, BG_BLEND_MULTIPLY, BG_BLEND_SCREEN, BG_BLEND_ADD, BG_BLEND_OVERLAY };
enum { BG_ALPHA_COMPOSITE = 1, BG_ALPHA_REPLACE = 2 };

struct BG_Stop {
    PF_FpLong pos, opacity;     // 0~1
    PF_FpLong r, g, b;          // sRGB 0~1
};

struct BG_PreRenderData {
    PF_LRect  in_rect, out_rect;
    A_long    shape, repeatMode, interp, blend, count, alphaMode;
    PF_FpLong sx, sy, ex, ey;   // 시작·끝 (레이어 좌표)
    PF_FpLong angleOff, contourSpan, cycles, phase, smooth, dither, amount;
    bool      reverse, preserveAlpha;
    BG_Stop   stops[BG_NUM_STOPS];
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
