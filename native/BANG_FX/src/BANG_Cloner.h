// BANG_Cloner.h — BANG Cloner : 소스 레이어의 현재 프레임을 N개 변환·합성하는 인스턴스 클로너 (SmartFX, 8/16/32 bpc)
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
#include <vector>

#define BANG_CLONER_MAJOR   1
#define BANG_CLONER_MINOR   6
#define BANG_CLONER_BUG     0
#define BANG_CLONER_STAGE   PF_Stage_DEVELOP
#define BANG_CLONER_BUILD   1

// 파라미터 인덱스 (0 = 입력 레이어). 그룹(topic) 포함 — AE 의 스트림 인덱스는 이 인덱스와 1:1 (그룹 시작/끝도 스트림)
enum {
    BC_INPUT = 0,
    BC_MODE,            // Layout: Linear | Grid | Radial
    BC_COUNT,           // Count (Linear·Radial)

    BC_G_LINEAR,        // ── Linear ──
    BC_ORIGIN,          //   Origin Index — 원본이 몇 번째인지 (1 기준)
    BC_DIR,             //   Direction: Horizontal | Vertical
    BC_GAP,             //   Gap — 이웃 클론 경계 사이 px, 음수 = 겹침
    BC_OFFSET,          //   Offset — 진행 방향과 수직으로 클론당 px
    BC_G_LINEAR_END,

    BC_G_GRID,          // ── Grid ──
    BC_COLS,            //   Columns
    BC_ROWS,            //   Rows
    BC_GAP_X,           //   Gap X
    BC_GAP_Y,           //   Gap Y
    BC_ORIGIN_X,        //   Origin X — 원본이 놓이는 열 (1 기준, ≤ Columns)
    BC_ORIGIN_Y,        //   Origin Y — 원본이 놓이는 행 (1 기준, ≤ Rows)
    BC_ORIGIN_QUICK,    //   [커스텀 UI] 9방향 퀵 버튼 → Origin X/Y
    BC_G_GRID_END,

    BC_G_RADIAL,        // ── Radial ──
    BC_RADIUS,          //   Radius
    BC_START_ANGLE,     //   Start Angle
    BC_START_QUICK,     //   [커스텀 UI] 각도 퀵 버튼 → Start Angle
    BC_SWEEP,           //   Sweep
    BC_SWEEP_QUICK,     //   [커스텀 UI] 각도 퀵 버튼 → Sweep
    BC_FACE_OUT,        //   Face Outward
    BC_CENTER_OBJ,      //   Center on Object — 켜면 원 중심 = 소스 내용 중심 (기본)
    BC_CENTER,          //   Center — 직접 지정한 원 중심 (Center on Object 가 꺼졌을 때)
    BC_G_RADIAL_END,

    BC_G_PATH,          // ── Path ──
    BC_PATH,            //   Mask Path — 이 레이어의 마스크 패스 (PF_Param_PATH)
    BC_PATH_ALIGN,      //   Align to Path — 클론을 진행 방향으로 회전
    BC_PATH_OFFSET,     //   Path Offset (%) — 시작 위치를 패스 길이의 % 만큼 이동
    BC_G_PATH_END,

    BC_G_STEP,          // ── Step ──
    BC_ROT_STEP,        //   Rotation Step (클론당 °)
    BC_ROT_QUICK,       //   [커스텀 UI] 각도 퀵 버튼 → Rotation Step
    BC_SCALE_STEP,      //   Scale Step (클론당 %)
    BC_OPACITY_END,     //   End Opacity (%)
    BC_G_STEP_END,

    BC_G_RANDOM,        // ── Random ──
    BC_SEED,            //   Seed
    BC_RAND_POS,        //   Random Position (px)
    BC_RAND_ROT,        //   Random Rotation (°)
    BC_RAND_SCALE,      //   Random Scale (%)
    BC_G_RANDOM_END,

    BC_NUM_PARAMS
};

enum { BC_MODE_LINEAR = 1, BC_MODE_GRID = 2, BC_MODE_RADIAL = 3, BC_MODE_PATH = 4 };
enum { BC_DIR_H = 1, BC_DIR_V = 2 };

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
