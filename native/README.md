# native/ — BANG 네이티브 이펙트 플러그인 (After Effects C++ SDK)

| 항목 | 내용 |
|---|---|
| 툴체인 | VS 2022 Build Tools (v143, MSVC 14.44) + Windows SDK 10 · MSBuild · SDK 의 `PiPLtool.exe` |
| SDK | 저장소 밖 `../../sdk/AfterEffectsSDK_26.5_win/Examples` (환경변수 `AE_SDK_DIR` 로 재지정 가능). **SDK 는 git 에 넣지 않는다** |
| 출력 | `native/out/Release/*.aex` (git 제외) → `tools/build.ps1` 이 zip 의 `plugins/` 에 동봉 |
| 설치 경로 | `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\BANG\` (관리자) |

## 빌드 · 설치 · 검증

```powershell
powershell -ExecutionPolicy Bypass -File native\build-native.ps1            # 빌드만
powershell -ExecutionPolicy Bypass -File native\build-native.ps1 -Install   # + 설치 (UAC)
powershell -ExecutionPolicy Bypass -File native\reload-in-ae.ps1            # AE 종료 → 빌드 → 설치 → AE 재시작 (개발 루프)
```

- **AE 실행 중엔 `.aex` 가 잠겨 복사가 조용히 실패**한다 → 반드시 `reload-in-ae.ps1` 로 종료 후 설치. 설치 후 해시 비교로 확인.
- 검증은 `saveFrameToPng` + 픽셀 프로브(`%TEMP%\png_probe.py` 류) 로 링 두께·색을 숫자로 확인. 32bpc 프로젝트는 `saveFrameToPng` 가 빈 PNG 를 내므로 뷰어 캡처로 확인.
- 디버그: `BANG_Stroke.vcxproj` 의 PreprocessorDefinitions 에 `BANG_FX_LOG` 를 추가하면 `%TEMP%\bang_stroke.log` 에 프리렌더/렌더 rect·포맷을 기록.

## 구조

```
native/
├── build-native.ps1        ← MSBuild 래퍼 (CL=/utf-8, 헤더의 C4819 회피)
├── reload-in-ae.ps1        ← 개발 루프 (AE 재시작 포함)
├── out/                    ← 산출물 (git 제외)
└── BANG_FX/
    ├── BANG_Stroke.vcxproj ← 이펙트 1개 = vcxproj 1개 (PiPL 커스텀 빌드 단계 포함)
    ├── BANG_Cloner.vcxproj
    └── src/BANG_{Stroke,Cloner}.{h,cpp}, BANG_{Stroke,Cloner}PiPL.r
```

## 이펙트

### BANG Stroke (`Pseudo 아님 · matchName "BANG Stroke"`, 카테고리 BANG · v1.2)
알파 경계의 부호 있는 거리(Felzenszwalb EDT, O(N))를 **한 번** 계산하고 그 거리장을 3겹의 획이 공유한다.
- `Stroke 1/2/3` 그룹(같은 구성): `Enable`(SUPERVISE) · `Position`(Outside/Center/Inside) · `Width (px)` · `Offset (px)` · `Softness (px)` · `Opacity` · `Blend`(Normal/Multiply/Screen/Add) · `Fill`(Solid/Gradient, SUPERVISE) · `Color` · 하위 그룹 `Gradient`(`Color B` · `Gradient Type` Across Stroke/Linear/Radial · `Gradient Angle` · `Gradient Scale (px)` · `Reverse`).
- `Edge Noise`: `Amount (px)` · `Scale (px)` · `Detail`(fBm 옥타브) · `Evolution`(각도 → 노이즈 3번째 축, 60° = 한 칸) · `Seed`. 거리장에 더하므로 모든 획이 같은 윤곽으로 흔들린다. 값 노이즈 fBm 은 ±1 을 못 채워 1.7배로 보정.
- `Body`: `Body`(Keep/Hide) · `Body Opacity` · `Order`. 합성은 premultiplied 로 누적: behind 획들 → 본체 → front 획들(각 획의 front 여부 = Order 가 In Front 이거나 Position ≠ Outside). 블렌드 모드는 그 시점의 누적 색을 base 로 쓴다.
- ECW: 꺼진 획 그룹과 Solid 일 때의 `Gradient` 하위 그룹을 `PF_UpdateParamUI` 로 회색(+접기) — `PF_OutFlag_SEND_UPDATE_PARAMS_UI` + `AEGP_RegisterWithAEGP` 필요.
- SmartFX, 8/16/32bpc, 멀티프레임 렌더 OK. 출력 버퍼를 (오프셋+두께+부드러움+2) 만큼 확장(`PF_OutFlag_I_EXPAND_BUFFER`).
- 거리장은 **출력 영역 + 여백 격자**에서 계산(AE 가 넘기는 입력 world 는 레이어 내용 경계로 잘려 있어 그 밖은 투명으로 채움).
- 안쪽/중앙 획은 항상 본체 위에 합성(Layer Style 과 동일), '합성 순서'는 바깥 획에만 적용.
- 다운샘플(해상도 1/2 등) 시 px 파라미터를 비율로 보정.

### BANG Cloner (`matchName "BANG Cloner"`, 카테고리 BANG)
소스 레이어에 적용하는 인스턴스 클로너 — 입력의 현재 프레임을 premultiplied float 로 한 번 변환해 두고, 클론마다 출력에 over 합성(Motion Tile 모델). 파라미터(영문, v1.2 = 그룹 5개):
- 최상위: `Layout`(Linear/Grid/Radial) · `Count`(Linear·Radial, Grid 에선 숨김)
- `Linear`: `Origin Index`(몇 번째 클론이 원본 자리인지, 1 기준) · `Direction`(Horizontal/Vertical) · `Gap`(이웃 클론 **경계 사이** px, 음수 = 겹침) · `Offset`(진행 방향과 수직으로 클론당 px)
- `Grid`: `Columns` · `Rows` · `Gap X/Y (px)` · `Origin X` / `Origin Y`(원본이 놓이는 칸, 1 기준; Columns/Rows 를 넘으면 `USER_CHANGED_PARAM` 에서 보정) · `Origin Preset`(3×3 퀵 버튼 — Anchor 컨트롤과 같은 굵은 화살표 + 가운데 Align Center 아이콘, Drawbot MoveTo/LineTo 다각형)
- `Radial`: `Radius (px)` · `Start Angle` + `Nudge` 버튼 한 줄 11개(−360 −90 −15 −2.5 −1 0 +1 +2.5 +15 +90 +360; 0 = 리셋, 나머지는 현재 값에 가감; 22px 정사각, 글자 9pt) · `Sweep (deg)` + `Preset` 버튼(절대값) · `Face Outward` · `Center`(레이어 좌표, 기본 50%)
- `Step`: `Rotation Step` + `Nudge` 버튼 · `Scale Step` · `End Opacity` — 단계는 **원본 클론을 0** 으로 앞뒤 누적
- `Path` (v1.7): `Path Layer`(PF_Param_LAYER) 로 셰이프 레이어 선택 → PreRender 에서 AEGP 로 읽음: 레이어 파라미터 스트림 값 `val.layer_id` → `AEGP_GetLayerFromLayerID` → `AEGP_GetNewStreamRefForLayer` → `ADBE Root Vectors Group` 을 재귀 순회(`ADBE Vector Group` 의 `ADBE Vector Transform Group` 앵커/위치/크기/회전 체인, `ADBE Vector Shape - Group` 의 `ADBE Vector Shape` 마스크 아웃라인, `- Rect`(둥근 모서리)·`- Ellipse` 는 베지어로 합성) → 셰이프 레이어 `AEGP_GetLayerToWorldXform` 으로 월드 → 대상 레이어 버퍼. 베지어는 16분할 길이 테이블로 균등 샘플. `Mask Path`(PF_PathQuerySuite) 는 폴백. 파라미터: `Start`/`End`/`Offset`/`Speed (%/s)`/`Reverse`/`Loop`/`Align to Path`/`Align Angle`.
- **좌표 공간(실측)**: 셰이프/텍스트 레이어에 적용된 이펙트의 **버퍼 좌표 = 컴프(월드) 좌표**(레이어 위치·회전과 무관, 컴프 크기 버퍼). 솔리드/푸티지의 버퍼 = 레이어 공간(좌상단 원점) → 레이어→월드 행렬의 역을 적용. 마스크 패스 좌표(PF_Path 스위트)도 셰이프 레이어에선 컴프 좌표로 온다(이전 "절반 빼기"는 위치가 우연히 컴프 중앙이라 맞아 보였던 것).
- `Bake to Layers`(PF_Param_BUTTON, USER_CHANGED_PARAM): 같은 `BuildClones` 로 변환 목록을 만들어 `AEGP_UtilitySuite6::AEGP_ExecuteScript` 로 ExtendScript 실행 — 복제본마다 앵커 = 피벗, 위치 = 피벗이 놓일 자리(셰이프면 컴프 좌표 그대로, AV 면 `sourcePointToComp`), 회전·크기·불투명도 가산, 이펙트 제거, 원본 숨김. 내용 경계는 `AEGP_GetLayerMaskedBounds`.
- MFR: PreRender 에서 AEGP 스위트를 쓰므로 `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` 을 켜지 않는다(렌더 내부 행 분할 스레드는 유지). `PF_OutFlag_NON_PARAM_VARY` 로 Speed 가 시간에 따라 다시 렌더.
- `Random`: `Seed` · `Random Position/Rotation/Scale`
- 퀵 버튼 = `PF_Param_CHECKBOX`(값 미사용, `CANNOT_TIME_VARY`) + `PF_PUI_CONTROL` 커스텀 컨트롤. `PF_OutFlag_CUSTOM_UI` + `register_ui`, `PF_Cmd_EVENT` 의 `PF_Event_DRAW`(Drawbot: AddRect/FillPath/StrokePath/DrawString Center 정렬) 와 `PF_Event_DO_CLICK`(`screen_point` 와 `effect_win.current_frame` 은 같은 좌표계) 로 그리기/클릭. 클릭 시 대상 파라미터 값을 바꾸고 `PF_ChangeFlag_CHANGED_VALUE` + `PF_InvalidateRect` + `PF_EO_UPDATE_NOW`.
- 그룹 표시: 활성 배치 그룹만 `PF_UpdateParamUI`(`PF_Param_GROUP_START`, `COLLAPSE_TWIRLY` 토글) 로 펼치고 나머지는 접음 + `PF_PUI_DISABLED` 로 회색 처리(그룹 헤더에도 먹음). 그룹 이름 앞 글리프(⋯ ▦ ◎ ∆ ⚄)는 UTF-8 이름으로 표시됨(SVG 아이콘은 불가; ↻ 는 Ʊ 처럼 읽혀 ∆ 로 교체). Grid/Radial/Step/Random 은 `PF_ParamFlag_START_COLLAPSED` 로 접힌 채 생성(Linear 만 펼침). **숨기지 않는 이유**: 그룹 스트림을 HIDDEN 하면 안의 커스텀 컨트롤(퀵 버튼) 본문 영역이 빈 칸으로 남는다(접기·ui_height 변경·그리기 생략 모두 무효, AE 2026). 커스텀 컨트롤이 없는 그룹만이라면 숨김 가능. `PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG` 로 그룹 기본 펼침(flags=0). 각도 파라미터는 `COLLAPSE_TWIRLY` 로 다이얼 접어 시작.
- Gap 은 소스 **내용 경계 크기**(`max_result_rect`) 를 더해 걸음 폭으로 바꾼다(`srcW + gap`) → 해상도가 커도 "여백 px" 만 조절. 피벗 = 내용 경계 중심: 회전·크기는 각 클론 자신의 중심 기준, Linear 는 원본 자리에서 출발.
- 출력 `max_result_rect` = 모든 클론으로 변환한 입력 경계의 합집합(+1px). `result_rect` = 요청 ∩ 최대 → 실제 버퍼는 요청 크기만. 입력은 전체(±100000)를 요청해 AE 가 레이어 경계로 자른다.
- 렌더 경로: (1) 회전·크기 없음 + 정수 이동 → 픽셀 직접 복사, (2) 그 외 → 역행렬 증분 바이리니어(float). 두 경로 모두 행 범위를 최대 8 스레드로 분할(`ParallelRows`). 실측 1440×2560 솔리드: 5 클론 ≈ +143 ms, 20 클론 회전 ≈ +186 ms(PNG 저장 포함).
- 항목 숨김(Grid 의 `Count`): AE 는 `PF_PUI_INVISIBLE` 을 동적으로 못 바꾸므로 `AEGP_DynamicStreamSuite2::AEGP_SetDynamicStreamFlag(HIDDEN)` 을 `PF_Cmd_UPDATE_PARAMS_UI`/`USER_CHANGED_PARAM`(`Layout` popup 은 `PF_ParamFlag_SUPERVISE`) 에서 호출. 플러그인 ID 는 GlobalSetup 의 `AEGP_RegisterWithAEGP`. 스트림 인덱스 = 파라미터 인덱스(그룹 시작/끝 포함).
- 실측(AE 2026, 100×100 사각형): Linear Count 4·Gap 50 → 중심 300/450/600/750, Origin Index 3 → 0/150/300/450, Gap −20 → 겹침 340 px 띠, Vertical+Offset 30 → (300,400)(330,550)(360,700), Scale Step −20% → 100/80/60/40 제자리 축소, Grid 3×2 Gap 50/25 Top Left → 열 300/450/600 · 행 400/525, Radial r200 → 상하좌우 200, 애니메이션 소스 → 클론 전부 같은 프레임.

## 겪은 함정
- `PF_REGISTER_EFFECT_EXT2` 매크로는 지역 변수 `result` 에 대입한다 — `return PF_REGISTER_EFFECT_EXT2(...)` 는 컴파일 오류.
- vcxproj 여러 개가 같은 `IntDir` 을 쓰면 MSB8028 경고 + 정리 오작동 → `out\obj\$(Configuration)\$(ProjectName)\`.
- 동적으로 숨긴 스트림은 **스크립트 `setValue` 가 실패**한다("property is hidden"). 스크립트로 `Layout` 을 바꾼 뒤 숨김 상태는 ECW 가 갱신될 때(UPDATE_PARAMS_UI) 반영되므로, 같은 evalScript 안에서 바로 값을 넣지 말고 **호출을 나눠**(UI 가 한 번 갱신된 뒤) 넣는다.
- `saveFrameToPng` 는 비동기 반환 → 렌더 시간은 PNG mtime 차이로 측정.
- 파라미터 이름은 사용자 요청으로 **영문**(v1.1). 한글이 필요하면 UTF-8 그대로 넘기면 AE 2026 에서 정상 표시된다 (CP949 변환하면 깨짐).
- `PF_OutFlag`/`OutFlags2` 값은 PiPL(.r) 과 GlobalSetup 이 동일해야 한다 (Stroke: 0x02000200 / 0x08001480, Cloner: 0x06000200 / 0x08001480 — SEND_UPDATE_PARAMS_UI 추가).
- 경계 픽셀 판정: 전경은 dIn, 배경은 dOut 만 본다 — 자기 자신은 항상 씨앗(거리 0)이라 둘 다 보면 전부 경계로 오판.
- SDK 헤더에 CP949 로 표현 불가한 문자가 있어 `/utf-8` 없이는 C4819 → 오류.
- PiPL `AE_Effect_Version` 은 `PF_VERSION(major,minor,bug,stage,build)` 와 같아야 한다: `(major<<19)|(minor<<15)|(bug<<11)|(stage<<9)|build` → 1.0.0 dev b1 = 524289, 1.1.0 = 557057.
- **그룹 안의 스트림을 숨기면** 펼쳐진 다이얼·커스텀 컨트롤의 본문이 ECW 에 남는다(제목만 사라짐) → 그룹은 숨기지 말고 **접는다**(`PF_UpdateParamUI` + `COLLAPSE_TWIRLY`). 리프(Count 같은 슬라이더) 숨김은 정상.
- `PF_Param_NO_DATA` 커스텀 컨트롤도 숨긴 그룹에서 행이 남는다 → 퀵 버튼은 체크박스형 + `PF_PUI_CONTROL` 로.
- **관리자 PowerShell 창 함정**: `deploy-local.ps1` 의 승격 창이 남아 있으면 computer-use 스크린샷에서 검은 사각형(마스크)으로 AE 위를 덮어 "ECW 가 깨졌다"는 착각을 일으킨다(이 세션에서 그룹/커스텀 UI 를 두 번 잘못 의심함). 화면이 이상하면 먼저 `Get-Process | ? MainWindowTitle` 로 `관리자: Windows PowerShell` 을 찾아 승격 `Stop-Process`.
- 설치 권한: `tools\grant-write-access.ps1` 한 번(UAC 1회) → `Plug-ins\BANG` 과 CEP 폴더에 사용자 Modify 권한 → 이후 복사에 UAC 불필요. `build-native.ps1 -Install` 은 쓰기 가능하면 직접 복사하고 해시로 검증(AE 실행 중이면 검증 실패로 알려줌).
- **같은 버전 번호의 `.aex` 를 바꿔 끼우면** AE 디스크 캐시가 이전 빌드의 렌더를 그대로 돌려준다(파라미터 상태가 같으면) → `reload-in-ae.ps1` 이 재시작 후 `app.purge(PurgeTarget.ALL_CACHES)`. 검증 전엔 반드시 퍼지.
- `applyNativeEffect` 의 프로브 Null 은 맨 위에 추가돼 레이어 인덱스가 1씩 밀린다 → 표현식엔 캡처한 인덱스 대신 `layer.index` 를 쓴다.
