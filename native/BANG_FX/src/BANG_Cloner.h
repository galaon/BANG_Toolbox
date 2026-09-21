// BANG_Cloner.h — BANG Cloner : 소스 레이어의 현재 프레임을 N개 변환·합성하는 인스턴스 클로너 (SmartFX, 8/16/32 bpc)
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
#include <vector>

#define BANG_CLONER_MAJOR   1
#define BANG_CLONER_MINOR   0
#define BANG_CLONER_BUG     0
#define BANG_CLONER_STAGE   PF_Stage_DEVELOP
#define BANG_CLONER_BUILD   1

// 파라미터 인덱스 (0 = 입력 레이어). 그룹(topic) 없이 평면 — 스트림 인덱스와 1:1 로 맞추기 위함
enum {
    BC_INPUT = 0,
    BC_MODE,          // 배치: 선형 | 그리드 | 방사형
    BC_COUNT,         // 복제 개수 (선형·방사형)
    BC_COLS,          // 열 (그리드)
    BC_ROWS,          // 행 (그리드)
    BC_MOVE_X,        // 이동 X (선형, 클론당 px)
    BC_MOVE_Y,        // 이동 Y (선형)
    BC_CELL_X,        // 칸 간격 X (그리드)
    BC_CELL_Y,        // 칸 간격 Y (그리드)
    BC_RADIUS,        // 반지름 (방사형)
    BC_START_ANGLE,   // 시작 각도 (방사형)
    BC_SWEEP,         // 각도 범위 (방사형)
    BC_FACE_OUT,      // 바깥쪽 향하기 (방사형)
    BC_CENTER,        // 중심 (레이어 좌표)
    BC_ROT_STEP,      // 회전 단계 (클론당 °)
    BC_SCALE_STEP,    // 크기 단계 (클론당 %)
    BC_OPACITY_END,   // 끝 불투명도 (%)
    BC_RAND_POS,      // 랜덤 위치 (px)
    BC_RAND_ROT,      // 랜덤 회전 (°)
    BC_RAND_SCALE,    // 랜덤 크기 (%)
    BC_SEED,          // 시드
    BC_NUM_PARAMS
};

enum { BC_MODE_LINEAR = 1, BC_MODE_GRID = 2, BC_MODE_RADIAL = 3 };

// 클론 하나의 변환: 출력 = [a b; c d]·소스 + [tx ty] (레이어 좌표) + 불투명도
struct BC_Xf { double a, b, c, d, tx, ty; float opacity; };

// 프리렌더 → 렌더로 넘기는 데이터
struct BC_PreRenderData {
    PF_LRect  in_rect;      // 체크아웃한 입력 영역 (레이어 좌표)
    PF_LRect  out_rect;     // result_rect (출력 world (0,0) 의 레이어 좌표)
    std::vector<BC_Xf> xf;  // 클론 변환 (인덱스 순서 = 그리는 순서)
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
