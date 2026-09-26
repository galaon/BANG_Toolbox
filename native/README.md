# native/ — BANG 네이티브 코드 (AE 이펙트 + 스포이드 도우미)

| 항목 | 내용 |
|---|---|
| 툴체인 | VS 2022 Build Tools (v143, MSVC 14.44) + Windows SDK 10 · MSBuild · SDK 의 `PiPLtool.exe` |
| SDK | 저장소 밖 `../../sdk/AfterEffectsSDK_26.5_win/Examples` (환경변수 `AE_SDK_DIR` 로 재지정 가능). **SDK 는 git 에 넣지 않는다** |
| 출력 | `native/out/Release/*.aex` (git 제외) → `tools/build.ps1` 이 zip 의 `plugins/` 에 동봉 |
| 스포이드 도우미 | `BANG_Picker/` → `out/Release/BANG_Picker.exe` → `build-native.ps1` 이 `com.bang.toolbox/bin/` 으로 복사 (AE SDK 무관, 패널이 직접 실행) |
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

### BANG Stroke (`Pseudo 아님 · matchName "BANG Stroke"`, 카테고리 BANG · v1.4)
알파 경계의 부호 있는 거리(Felzenszwalb EDT, O(N))로 **획 하나**를 그린다.
- **여러 겹 = 이펙트를 여러 번 적용**. 두 번째 인스턴스는 첫 획이 포함된 알파를 입력으로 받으므로 SDF 가 그 바깥 윤곽을 따라 다시 계산된다 — 별도 로직이 필요 없다. (v1.2 의 `Stroke 1/2/3` 그룹은 그룹을 회색처리하면 그 안 `Enable` 까지 비활성화돼 폐기.)
- 파라미터(평평): `Position`(Outside/Center/Inside) · `Corner`(Round/Miter/Bevel) · `Miter Limit` · `Width (px)` · `Offset (px)` · `Softness (px)` · `Opacity` · `Blend`(Normal/Multiply/Screen/Add) · `Fill`(Solid/Gradient) · `Color` · 그룹 `Gradient`(`Color B` · `Gradient Type` Across Stroke/Linear/Radial · `Gradient Angle` · `Gradient Scale (px)` · `Opacity A` · `Opacity B` · `Reverse`). 그라데이션 불투명도는 색과 같은 t 로 보간해 획 알파에 곱한다.
- 모서리(`SharpenCorners`, Corner ≠ Round 일 때만 실행 — Round 는 추가 비용 0): 이진 마스크의 계단 때문에 "최근접 씨앗을 공유하는 부채꼴"으로는 회전한 도형의 꼭지점 각도를 맞출 수 없다(0°/45° 는 맞고 10°/22.5° 는 과하거나 모자람 — 실측함). 대신 **안티에일리어싱된 알파**를 `[1 4 6 4 1]/16` 로 한 번 고르게 만든 뒤 그 기울기로 경계 픽셀마다 바깥 법선 n 과 0.5 등고선까지의 거리 t 를 구한다(1픽셀 차분은 거의 수평/수직인 변에서 법선을 축에 양자화한다). `|∇a| < 0.12` 거나 `|t| > 1.5` 면 버린다 — 흐릿한 픽셀은 t 가 발산해 가짜 평면이 최댓값을 이긴다. 주변 법선이 30° 넘게 벌어지는 픽셀 = 꼭지점(flag 2), 그리고 꼭지점에서 3px 이내는 평면 출처에서 제외(흐려진 법선이 이등분선 쪽으로 기울어 마이터를 무딜게 만든다). 최근접 씨앗 근처에 꼭지점이 있는 픽셀만 손대고, 그 꼭지점 반경 8px 안의 ‘깨끗한 변’ 지지 평면 거리 중 **최댓값**이 곷 Miter 거리. 그 값이 둘렉거리보다 크면 볼록한 꼭지점이 아니므로(오목한 모서리) 그대로 둔다. Bevel 은 양 끝 법선의 이등분 평면을 더해 꼭지점을 잘라내며, Miter Limit 초과 시에도 같은 식을 쓴다. 직선 구간은 손대지 않으므로 기울어진 변의 획 두께가 변하지 않는다.
- `Edge Noise`: `Amount (px)` · `Scale (px)` · `Detail`(fBm 옥타브) · `Evolution`(각도 → 노이즈 3번째 축, 60° = 한 칸) · `Seed`. 거리장에 더해 가장자리를 흔든다. 값 노이즈 fBm 은 ±1 을 못 채워 1.7배로 보정.
- `Body`: `Body`(Keep/Hide) · `Body Opacity` · `Order`. 합성은 premultiplied 누적: behind 획 → 본체 → front 획(front 여부 = Order 가 In Front 이거나 Position ≠ Outside). 여기서 "본체" = 이 인스턴스의 입력이므로 아래쪽 획들까지 포함된다. 블렌드 모드는 그 시점의 누적 색을 base 로 쓴다.
- SmartFX, 8/16/32bpc, 멀티프레임 렌더 OK. 출력 버퍼를 (오프셋+두께+부드러움+2) 만큼 확장(`PF_OutFlag_I_EXPAND_BUFFER`).
- 거리장은 **출력 영역 + 여백 격자**에서 계산(AE 가 넘기는 입력 world 는 레이어 내용 경계로 잘려 있어 그 밖은 투명으로 채움).
- 안쪽/중앙 획은 항상 본체 위에 합성(Layer Style 과 동일), Order 는 바깥 획에만 적용.
- 다운샘플(해상도 1/2 등) 시 px 파라미터를 비율로 보정.
- 성능(1440×2560 텍스트 · 획 60px, 거리장 계산 기준): Round 10 ms · Bevel 27 ms · Miter 37 ms. 핵심은 ① 격자 = 입력∪출력 영역(예전엔 출력+margin 사방, Miter 는 margin 이 Limit 배라 2.5배였다), ② 획이 닿지 않는 쪽 거리장은 EDT·보정 생략(`bandLo/bandHi`), ③ 지지 평면을 꼭지점당 한 번 모으고 같은 변끼리 평균내 2~4개로 줄인 것(띄 픽셀마다 17×17 ×2 → 175 ms가 3 ms 로). 평면을 평균 대신 ‘가장 바깥’ 을 고르면 AA 잡음만큼 밀려 모서리가 1~2px 과하게 뻗는다. edt2d 병렬화도 해 봤으나 열 패스가 대역폭 병목이라 이득이 적고 작은 격자에선 오히려 느려져 되돌렸다.
- 모서리 보정의 안전장치(없으면 글자에서 바로 티난다): ① 서로 다른 변 **둘**을 찾은 픽셀만 보정(하나만 있으면 Limit 가 적용되지 않아 뿔이 생긴다), ② `dm ≥ r/limit` 로 뻗음을 하드 클램프, ③ cos(각/2) < 0.25 인 날카로운 각은 베벨 현(1/cos 발산) 대신 한계치로 절단, ④ **꼭지점 부채꼴 밖은 손대지 않음**(부채꼴 경계에선 평면 거리 = 둘렉거리 라 이음새가 없다. 이걸 안 하면 외곽에 1px 계단이 줄지어 생긴다), ⑤ 꼭지점 판정 45°(30° 면 곱선이 꼭지점으로 잡힌다).
- ⚠ 법선 계산 루프는 `i±1`·`i±w` 를 읽는다 — 내용 상자를 즐일 때 끝을 `w-2`/`h-2` 로 막지 않으면 버퍼 밖을 읽어 AE 가 죽는다(1.3.10 에서 실제로 발생).
- Stroke 파라미터 순서: Width → Position → Corner → Miter Limit → Offset → … (v1.5). `Fill` 은 SUPERVISE 로, Solid 이면 `Gradient` 그룹을 회색+접음 — 회색 처리하는 그룹 안에 다시 켜는 컨트롤을 두지 않는다는 규칙은 그대로(Fill 은 그룹 밖).
- 프로파일링: `build-native.ps1 -Install -Defines BANG_FX_LOG` → `%TEMP%\bang_stroke.log` 에 BuildSDF·Sharpen 단계별 시간과 꼭지점·평면 개수가 쌓인다.

### BANG Gradient (`matchName "BANG Gradient"`, 카테고리 BANG · v1.0)
정지점 최대 8개짜리 그라데이션. AE SDK 에는 그라데이션 파라미터 타입이 없으므로
정지점마다 (색 + 위치 + 불투명도) 세 파라미터를 깔고, `Stops` 밖의 것은 `PF_UpdateParamUI` 로 회색 처리한다.
- `Stops` 를 바꾸면 USER_CHANGED_PARAM 에서 위치를 고르게 다시 쓴다 — 값 변경은 `params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE`.
- 모양: Linear/Radial/Angular/Diamond/Reflected 는 Start·End 두 점에서 t 를 구하고,
  **Contour** 는 알파 경계까지의 거리(Felzenszwalb EDT, BANG Stroke 와 같은 코드)를 `Contour Span` 으로 나눠 t 로 쓴다.
- 보간: sRGB 그대로 / 선형 RGB / OKLab / OKLCh(극좌표, 짧은·긴 색상 경로). OKLab 변환은 Björn Ottosson 계수.
- Dither 는 t 에 ±0.5LSB 크기의 해시 잡음을 더해 8bpc 띄를 지운다.
- 이음매 AA: Repeat·Angular 은 t 가 1→0 으로 끊긴다. 옆·위·아래 픽셀의 t 와 0.45 넘게 벌어지는 픽셀만 4×4 서브샘플. t 를 픽셀마다 다시 구하면 비싸서 세 줄(이전·현재·다음)을 돌려 쓴다.
- `Fit Horizontal`·`Fit Vertical`·`Fit Diagonal`: 버튼(SUPERVISE) → `AEGP_ExecuteScript` 로 sourceRectAtTime 을 재 값이나 표현식을 넣는다. 방향은 **지금 Start→End 를 보고 결정**한다 — 가로는 좌↔우, 세로는 상↔하로 뒤집고, 대각선은 현재 각이 대각선 위면 다음 사분면(45°→135°→225°→315°, 화면 좌표라 각이 커지는 쪽이 시계방향), 아니면 45° 부터. ⚠ **셰이프·텍스트 레이어는 점 파라미터가 컴 좌표**이다(버퍼가 컴 크기). sourceRectAtTime 은 소스 좌표라 `position - anchorPoint` 만큼 옳겨야 맞는다 (실측으로 확인: 회전·스케일은 이펙트 뒤에 적용돼 보정 불필요, 그러나 Position 은 버퍼 좌표에 그대로 반영된다). 솔리드·푸테지는 보정 없음.
- 교환 포맷: `.css`(colorffy·coolors 식 `linear-gradient(<ang>deg[ in oklab], rgba() p%, …)`, 각도·보간 색공간 왕복) · `.ggr`(GIMP, 문서화된 텍스트) · `.json`(자체). Photoshop `.grd` 는 비공개 바이너리(직렬화된 액션 디스크립터)라 제외.
- ⚠ **숨긴 스트림은 스크립트 setValue 가 안 된다** — 정지점 줄을 DynamicStream 으로 숨기므로, 파일 임포트 전에 `ApplyStopVisibility(in_data, BG_NUM_STOPS)` 로 전부 펼쳐둔다. 안 그러면 세 번째 이후 정지점이 조용히 빠진다(실제로 겪음). 반면 `params[]` + `change_flags` 는 숨김 여부와 무관하게 쓴다.
- `Randomize`: 감마(gamut) 경계를 직접 찾아 쓴다. `BG_MaxChroma(L,h)` = 그 밝기·색상에서 sRGB 안에 들어오는 최대 채도(이분탐색 18회), `BG_CuspL(h)` = 그 색상이 가장 진해지는 밝기. 채널을 잘라내면(예전 방식) 색상이 틀어지고 채도가 빠져 탁해진다 — 그래서 처음부터 만들 수 있는 범위 안에서만 고른다. ‘똑색’ 은 결국 **어두운 주황~노랑**이므로 OKLCh h≈88° ±85° 띄에서만 밝기 바닥을 `0.30 + 0.56·k` 로 올린다(k = 코사인 창). 다른 색상은 `cuspL − 0.34` 만 바닥이라 짙은 남색·버건디가 그대로 나온다. (Wijffelaars cusp 삼각형 + gamut-relative saturation — meodai/cusphanger 와 같은 접근)
- 정지점은 `Stop N` 그룹(`START_COLLAPSED`) 으로 묶어 ECW 높이를 줄였다. ⚠ **함정 두 개**: ① AEGP 로 줄을 숨길 때 **그룹을 먼저 숨기면 그 뒤 인덱스 조회가 어긋난다**(안 쓰는 정지점이 통째로 다 드러났다) — 자식 세 줄을 먼저 숨기고 그룹을 마지막에. ② 그룹은 **ECW 표시에만** 있다 — 스크립트에는 여전히 평평해서 `fx.property('Color 1')` 이 그대로 동작한다(한 단계 들어가면 오히려 안 된다).
- `Alpha = Replace`(**기본값**) 는 그라데이션 불투명도를 over 합성 대신 출력 알파로 쓴다(`oa = (preserveAlpha?ba:1) * ga`).
- Stops Bar: `PF_PUI_CONTROL` 체크박스 + Drawbot. 칩 클릭 → `PF_AppColorPickerDialog` → `uu.change_flags = PF_ChangeFlag_CHANGED_VALUE`. 커스텀 UI 를 쓰려면 `PF_OutFlag_CUSTOM_UI` 를 GlobalSetup 과 PiPL 에 둘 다 넣어야 한다(안 넣으면 "no custom ui outflag" 오류).
- 버퍼 확장이 없으므로 `I_EXPAND_BUFFER` 없이 PiPL OutFlags = DEEP_COLOR_AWARE | SEND_UPDATE_PARAMS_UI (0x06000000).

#### Fill Gaps (획 안쪽에 남는 구멍 메우기)
- `BuildGapMask()`: 구멍 후보 = 알파 밖 + `sdf > center+half+0.5` → 4방향 연결성분 → 격자 테두리에 닿는 성분(바깥 배경)은 제외. 남은 성분의 안쪽 반지름 = `max(sdf) − (center+half)`, 지름이 `Gap Size` 보다 작으면 채운다. 별도 EDT 없이 기존 거리장만 쓴다.
- 채운 지역은 배경 쪽으로만 `2 + ceil(softness)` 픽셀 넓혐 획과의 반투명 이음매를 덮는다. `alpha ≥ 0.5` 로는 넓히지 않는다 — 안 그러면 Inside 획에서 본체 위에 획 색이 번진다.
- ⚠ Inside 획은 `bandHi ≤ 0` 이라 바깥 EDT 를 건너뛰어 구멍 크기를 재지 못한다 — `gapMode != Off` 이면 `bandHi` 를 최소 2 로 올린다.
- 파라미터는 **맨 뒤에** 붙였다(그래야 기존 인스턴스의 값이 안 밀린다).

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

## BANG_Picker.exe — 화면 스포이드 도우미

패널의 Color Picker 가 `window.cep.process.createProcess(exe, <결과파일>)` 로 실행한다.
고르면 결과파일에 `#RRGGBB` 를 쓰고 0 으로, 취소하면 파일 없이 1 로 끝난다.
진단용 플래그: `-probe`(화면 계측값만 쓰고 종료) · `-probe2`(창 rect·가시성·포그라운드 여부).

**왜 네이티브인가** — CEP(Chromium 99) 에서 화면 픽셀을 읽는 브라우저 경로는 전부 막혔 있다. 실측:
`window.EyeDropper` 는 **존재하지만** `open()` 이 2ms 만에 `AbortError` (CEF 가 오버레이를 못 띄운다),
`navigator.mediaDevices.getDisplayMedia` 는 `NotAllowedError: Permission denied`. 다시 조사하지 말 것.

구조는 PowerToys Color Picker · Just Color Picker 와 같다:
화면을 `BitBlt(SRCCOPY | CAPTUREBLT)` 로 한 번 떠서 DIB 에 담고, 그 정지화면을 가상화면 전체 크기의
`WS_POPUP | WS_EX_TOPMOST` 창에 깔고, 커서 주변을 `StretchBlt` + `COLORONCOLOR`(최근접)로 확대해 보여준다.

☠ **하루를 날릴 번한 함정 세 개** (전부 실측으로 잡았다):

1. **CEP 가 자식을 `STARTUPINFO.wShowWindow = SW_HIDE` 로 띄운다.** Windows 는 프로세스의 **첫 `ShowWindow` 호출**을
   그 값으로 가로채기 때문에 한 번만 부르면 창이 영영 안 뜼다 — 그런데 프로세스는 살아있고 `GetWindowRect`·`IsWindowVisible` 도
   정상으로 나와서 원인을 찾기 아주 어렵다. `ShowWindow` 를 **두 번** 부른다.
2. **포그라운드를 못 가져온다.** 백그라운드 프로세스(AE)가 띄운 창은 `SetForegroundWindow` 가 먹지 않아
   `WM_MOUSEMOVE`·`WM_LBUTTONDOWN`·`WM_KEYDOWN` 이 아예 안 온다(`SetCapture` 도 마찬가지). 그래서 입력은
   `WH_MOUSE_LL` · `WH_KEYBOARD_LL` 저수준 후크로 받는다. 후크에서 `return 1` 로 **입력을 삼켜야** 밑에 깔린 AE 로
   클릭이 새지 않는다(안 그러면 색 고를 때마다 레이어가 선택되거나 끌린다).
3. **색은 화면이 아니라 캐처 버퍼에서 읽는다.** 안 그러면 우리가 그린 루페·격자가 결과 색에 섞인다.

그 밖에: 고DPI 좌표가 어긋나지 않게 `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`,
다중 모니터는 `SM_XVIRTUALSCREEN` 기준(원점이 음수일 수 있다), 재취드는 루페 영역만 `InvalidateRect`
(4K 전체를 매번 칠하면 느리다), 스포이드를 부른 클릭의 떼기를 한 번 삼킨 뒤부터 입력을 센다(`g_armed`).
