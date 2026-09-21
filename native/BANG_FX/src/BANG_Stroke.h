// BANG_Stroke.h — BANG Stroke : 알파 경계 거리 변환 기반 획 이펙트 (SmartFX, 8/16/32 bpc)
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
#define BANG_STROKE_MINOR   1
#define BANG_STROKE_BUG     0
#define BANG_STROKE_STAGE   PF_Stage_DEVELOP
#define BANG_STROKE_BUILD   1

// 파라미터 인덱스 (0 = 입력 레이어)
enum {
    BS_INPUT = 0,
    BS_POSITION,      // 획 위치: 바깥 | 중앙 | 안쪽
    BS_WIDTH,         // 두께 px
    BS_OFFSET,        // 오프셋 px (가장자리에서 띄우기)
    BS_COLOR,         // 색
    BS_OPACITY,       // 불투명도 %
    BS_SOFTNESS,      // 부드러움 px
    BS_BODY,          // 본체: 유지 | 숨김(획만)
    BS_ORDER,         // 합성 순서: 획을 뒤에 | 획을 앞에
    BS_NUM_PARAMS
};
enum { BS_DISK_POSITION = 1, BS_DISK_WIDTH, BS_DISK_OFFSET, BS_DISK_COLOR, BS_DISK_OPACITY, BS_DISK_SOFTNESS, BS_DISK_BODY, BS_DISK_ORDER };

enum { BS_POS_OUTSIDE = 1, BS_POS_CENTER = 2, BS_POS_INSIDE = 3 };
enum { BS_BODY_KEEP = 1, BS_BODY_HIDE = 2 };
enum { BS_ORDER_BEHIND = 1, BS_ORDER_FRONT = 2 };

// 프리렌더 → 렌더로 넘기는 데이터
struct BS_PreRenderData {
    PF_LRect  in_rect;      // 체크아웃한 입력 영역 (레이어 좌표)
    PF_LRect  out_rect;     // result_rect (출력 world (0,0) 의 레이어 좌표)
    A_long    margin;       // 입력 요청 시 넓힌 여백
    A_long    position;
    PF_FpLong width, offset, opacity, softness;
    PF_Pixel  color;        // 8bpc 색 (렌더 시 정규화)
    A_long    body, order;
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
