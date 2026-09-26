// BANG_Stroke.h — BANG Stroke : 알파 경계 거리 변환 기반 획 이펙트 (SmartFX, 8/16/32 bpc)
//  v1.3: 획 하나 = 이펙트 하나. 여러 겹은 이펙트를 여러 번 적용해서 만든다
//        (두 번째 인스턴스는 첫 획이 포함된 알파를 입력으로 받으므로 자연히 그 바깥에 그려진다)
#pragma once

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include "AE_EffectSuitesHelper.h"

#define BANG_STROKE_MAJOR   1
#define BANG_STROKE_MINOR   6
#define BANG_STROKE_BUG     0
#define BANG_STROKE_STAGE   PF_Stage_DEVELOP
#define BANG_STROKE_BUILD   1

// 파라미터 인덱스 (0 = 입력 레이어)
enum {
    BS_INPUT = 0,
    BS_WIDTH,           // Width (px)
    BS_POSITION,        // Position: Outside | Center | Inside
    BS_CORNER,          // Corner: Round | Miter | Bevel
    BS_MITER_LIMIT,     // Miter Limit (Miter 일 때 뾰족함 한계, 넘으면 Bevel)
    BS_OFFSET,          // Offset (px) — 가장자리에서 띄우기
    BS_SOFTNESS,        // Softness (px)
    BS_OPACITY,         // Opacity (%)
    BS_BLEND,           // Blend: Normal | Multiply | Screen | Add
    BS_FILL,            // Fill: Solid | Gradient
    BS_COLOR,           // Color (Solid · Gradient 시작)

    BS_G_GRAD,          // ── Gradient ──
    BS_COLOR_B,         //   Color B
    BS_GRAD_TYPE,       //   Type: Across Stroke | Linear | Radial
    BS_GRAD_ANGLE,      //   Angle (Linear)
    BS_GRAD_SCALE,      //   Scale (px, Linear·Radial)
    BS_GRAD_OP_A,       //   Opacity A (%) — 그라데이션 시작 쪽 불투명도
    BS_GRAD_OP_B,       //   Opacity B (%) — 끝 쪽 불투명도
    BS_GRAD_REV,        //   Reverse
    BS_G_GRAD_END,

    BS_G_NOISE,         // ── Edge Noise ──
    BS_N_AMOUNT,        //   Amount (px)
    BS_N_SCALE,         //   Scale (px)
    BS_N_DETAIL,        //   Detail (fBm 옥타브)
    BS_N_EVOLUTION,     //   Evolution (각도, 애니메이션 가능)
    BS_N_SEED,          //   Seed
    BS_G_NOISE_END,

    BS_G_BODY,          // ── Body ── (여기서 Body = 이 이펙트의 입력 = 아래쪽 획까지 포함)
    BS_BODY,            //   Body: Keep | Hide (stroke only)
    BS_BODY_OPACITY,    //   Body Opacity (%)
    BS_ORDER,           //   Order: Stroke Behind | Stroke In Front (바깥 획에만 의미)
    BS_G_BODY_END,

    // ⚠ 새 파라미터는 **끝에만** 붙인다 — 중간에 끼우면 기존 인스턴스의 값이 밀린다.
    BS_G_FILL,          // ── Fill Gaps ──
    BS_FILLGAP,         //   Fill Gaps: Off | Narrow Gaps | All Counters
    BS_FILLGAP_SIZE,    //   Gap Size (px) — Narrow Gaps 일 때 '좁다' 의 기준(지름)
    BS_G_FILL_END,

    BS_NUM_PARAMS
};

enum { BS_GAP_OFF = 1, BS_GAP_NARROW = 2, BS_GAP_ALL = 3 };

enum { BS_POS_OUTSIDE = 1, BS_POS_CENTER = 2, BS_POS_INSIDE = 3 };
enum { BS_CORNER_ROUND = 1, BS_CORNER_MITER = 2, BS_CORNER_BEVEL = 3 };
enum { BS_BODY_KEEP = 1, BS_BODY_HIDE = 2 };
enum { BS_ORDER_BEHIND = 1, BS_ORDER_FRONT = 2 };
enum { BS_FILL_SOLID = 1, BS_FILL_GRADIENT = 2 };
enum { BS_GRAD_ACROSS = 1, BS_GRAD_LINEAR = 2, BS_GRAD_RADIAL = 3 };
enum { BS_BLEND_NORMAL = 1, BS_BLEND_MULTIPLY = 2, BS_BLEND_SCREEN = 3, BS_BLEND_ADD = 4 };

// 프리렌더 → 렌더로 넘기는 데이터 (px 값은 현재 해상도 기준)
struct BS_PreRenderData {
    PF_LRect  in_rect;      // 체크아웃한 입력 영역 (레이어 좌표)
    PF_LRect  out_rect;     // result_rect (출력 world (0,0) 의 레이어 좌표)
    A_long    margin;       // 입력 요청 시 넓힌 여백
    A_long    position, corner, fill, gradType, blend, body, order, gapMode;
    PF_FpLong gapSize;
    PF_FpLong width, offset, softness, opacity, gradAngle, gradScale, bodyOpacity;
    PF_FpLong miterLimit, gradOpA, gradOpB;
    PF_FpLong bandLo, bandHi;   // 획이 닿는 거리 범위(부호 있는 거리) — 모서리 보정 범위 제한용
    bool      gradRev, front;
    PF_Pixel  colorA, colorB;
    PF_FpLong noiseAmount, noiseScale, noiseEvo;
    A_long    noiseDetail, noiseSeed;
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
