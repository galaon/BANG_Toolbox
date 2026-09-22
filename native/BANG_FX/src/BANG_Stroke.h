// BANG_Stroke.h — BANG Stroke : 알파 경계 거리 변환 기반 획 이펙트 (SmartFX, 8/16/32 bpc)
//  v1.2: 획 3겹 · 그라데이션 채우기 · 블렌드 모드 · 가장자리 노이즈 · 본체 불투명도
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
#define BANG_STROKE_MINOR   2
#define BANG_STROKE_BUG     0
#define BANG_STROKE_STAGE   PF_Stage_DEVELOP
#define BANG_STROKE_BUILD   1

#define BS_NUM_STROKES 3

// 파라미터 인덱스 (0 = 입력 레이어). 획 3겹은 같은 구성이 반복된다.
enum {
    BS_INPUT = 0,

    BS_S1_GROUP,        // ── Stroke 1 ──
    BS_S1_ON,           //   Enable
    BS_S1_POSITION,     //   Position: Outside | Center | Inside
    BS_S1_WIDTH,        //   Width (px)
    BS_S1_OFFSET,       //   Offset (px)
    BS_S1_SOFTNESS,     //   Softness (px)
    BS_S1_OPACITY,      //   Opacity (%)
    BS_S1_BLEND,        //   Blend: Normal | Multiply | Screen | Add
    BS_S1_FILL,         //   Fill: Solid | Gradient
    BS_S1_COLOR,        //   Color (Solid · Gradient 시작)
    BS_S1_GRAD_GROUP,   //   ── Gradient ──
    BS_S1_COLOR_B,      //     Color B
    BS_S1_GRAD_TYPE,    //     Type: Across Stroke | Linear | Radial
    BS_S1_GRAD_ANGLE,   //     Angle (Linear)
    BS_S1_GRAD_SCALE,   //     Scale (px, Linear·Radial)
    BS_S1_GRAD_REV,     //     Reverse
    BS_S1_GRAD_GROUP_E,
    BS_S1_GROUP_END,

    BS_S2_GROUP, BS_S2_ON, BS_S2_POSITION, BS_S2_WIDTH, BS_S2_OFFSET, BS_S2_SOFTNESS, BS_S2_OPACITY, BS_S2_BLEND,
    BS_S2_FILL, BS_S2_COLOR, BS_S2_GRAD_GROUP, BS_S2_COLOR_B, BS_S2_GRAD_TYPE, BS_S2_GRAD_ANGLE, BS_S2_GRAD_SCALE, BS_S2_GRAD_REV, BS_S2_GRAD_GROUP_E, BS_S2_GROUP_END,

    BS_S3_GROUP, BS_S3_ON, BS_S3_POSITION, BS_S3_WIDTH, BS_S3_OFFSET, BS_S3_SOFTNESS, BS_S3_OPACITY, BS_S3_BLEND,
    BS_S3_FILL, BS_S3_COLOR, BS_S3_GRAD_GROUP, BS_S3_COLOR_B, BS_S3_GRAD_TYPE, BS_S3_GRAD_ANGLE, BS_S3_GRAD_SCALE, BS_S3_GRAD_REV, BS_S3_GRAD_GROUP_E, BS_S3_GROUP_END,

    BS_N_GROUP,         // ── Edge Noise ── (모든 획의 가장자리를 함께 흔든다)
    BS_N_AMOUNT,        //   Amount (px)
    BS_N_SCALE,         //   Scale (px)
    BS_N_DETAIL,        //   Detail (옥타브 수)
    BS_N_EVOLUTION,     //   Evolution (각도, 애니메이션 가능)
    BS_N_SEED,          //   Seed
    BS_N_GROUP_END,

    BS_B_GROUP,         // ── Body ──
    BS_BODY,            //   Body: Keep | Hide (stroke only)
    BS_BODY_OPACITY,    //   Body Opacity (%)
    BS_ORDER,           //   Order: Stroke Behind | Stroke In Front (바깥 획에만 의미)
    BS_B_GROUP_END,

    BS_NUM_PARAMS
};

// 한 획의 파라미터 인덱스 묶음 (i = 0..2)
struct BS_ParamIdx { int group, on, position, width, offset, softness, opacity, blend, fill, color, gradGroup, colorB, gradType, gradAngle, gradScale, gradRev; };
static const BS_ParamIdx BS_IDX[BS_NUM_STROKES] = {
    { BS_S1_GROUP, BS_S1_ON, BS_S1_POSITION, BS_S1_WIDTH, BS_S1_OFFSET, BS_S1_SOFTNESS, BS_S1_OPACITY, BS_S1_BLEND, BS_S1_FILL, BS_S1_COLOR, BS_S1_GRAD_GROUP, BS_S1_COLOR_B, BS_S1_GRAD_TYPE, BS_S1_GRAD_ANGLE, BS_S1_GRAD_SCALE, BS_S1_GRAD_REV },
    { BS_S2_GROUP, BS_S2_ON, BS_S2_POSITION, BS_S2_WIDTH, BS_S2_OFFSET, BS_S2_SOFTNESS, BS_S2_OPACITY, BS_S2_BLEND, BS_S2_FILL, BS_S2_COLOR, BS_S2_GRAD_GROUP, BS_S2_COLOR_B, BS_S2_GRAD_TYPE, BS_S2_GRAD_ANGLE, BS_S2_GRAD_SCALE, BS_S2_GRAD_REV },
    { BS_S3_GROUP, BS_S3_ON, BS_S3_POSITION, BS_S3_WIDTH, BS_S3_OFFSET, BS_S3_SOFTNESS, BS_S3_OPACITY, BS_S3_BLEND, BS_S3_FILL, BS_S3_COLOR, BS_S3_GRAD_GROUP, BS_S3_COLOR_B, BS_S3_GRAD_TYPE, BS_S3_GRAD_ANGLE, BS_S3_GRAD_SCALE, BS_S3_GRAD_REV },
};

enum { BS_POS_OUTSIDE = 1, BS_POS_CENTER = 2, BS_POS_INSIDE = 3 };
enum { BS_BODY_KEEP = 1, BS_BODY_HIDE = 2 };
enum { BS_ORDER_BEHIND = 1, BS_ORDER_FRONT = 2 };
enum { BS_FILL_SOLID = 1, BS_FILL_GRADIENT = 2 };
enum { BS_GRAD_ACROSS = 1, BS_GRAD_LINEAR = 2, BS_GRAD_RADIAL = 3 };
enum { BS_BLEND_NORMAL = 1, BS_BLEND_MULTIPLY = 2, BS_BLEND_SCREEN = 3, BS_BLEND_ADD = 4 };

// 획 하나의 렌더 파라미터 (현재 해상도 px)
struct BS_StrokeData {
    bool      on;
    A_long    position, fill, gradType, blend;
    PF_FpLong width, offset, softness, opacity, gradAngle, gradScale;
    bool      gradRev;
    PF_Pixel  colorA, colorB;
    bool      front;        // 본체 위에 그릴지 (Order + Position 으로 결정)
};

// 프리렌더 → 렌더로 넘기는 데이터
struct BS_PreRenderData {
    PF_LRect  in_rect;      // 체크아웃한 입력 영역 (레이어 좌표)
    PF_LRect  out_rect;     // result_rect (출력 world (0,0) 의 레이어 좌표)
    A_long    margin;       // 입력 요청 시 넓힌 여백
    BS_StrokeData strokes[BS_NUM_STROKES];
    A_long    body, order;
    PF_FpLong bodyOpacity;
    PF_FpLong noiseAmount, noiseScale, noiseEvo;
    A_long    noiseDetail, noiseSeed;
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
