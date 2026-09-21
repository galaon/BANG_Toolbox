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

### BANG Stroke (`Pseudo 아님 · matchName "BANG Stroke"`, 카테고리 BANG)
알파 경계의 부호 있는 거리(Felzenszwalb EDT, O(N))로 획을 그린다. 파라미터(한글): 획 위치(바깥/중앙/안쪽) · 두께 · 오프셋 · 색 · 불투명도 · 부드러움 · 본체(유지/숨김) · 합성 순서(뒤/앞).
- SmartFX, 8/16/32bpc, 멀티프레임 렌더 OK. 출력 버퍼를 (오프셋+두께+부드러움+2) 만큼 확장(`PF_OutFlag_I_EXPAND_BUFFER`).
- 거리장은 **출력 영역 + 여백 격자**에서 계산(AE 가 넘기는 입력 world 는 레이어 내용 경계로 잘려 있어 그 밖은 투명으로 채움).
- 안쪽/중앙 획은 항상 본체 위에 합성(Layer Style 과 동일), '합성 순서'는 바깥 획에만 적용.
- 다운샘플(해상도 1/2 등) 시 px 파라미터를 비율로 보정.

### BANG Cloner (`matchName "BANG Cloner"`, 카테고리 BANG)
소스 레이어에 적용하는 인스턴스 클로너 — 입력의 현재 프레임을 premultiplied float 로 한 번 변환해 두고, 클론마다 역행렬 바이리니어 샘플링으로 출력에 over 합성(Motion Tile 모델). 파라미터(한글, 평면 — 그룹 없음): 배치(선형/그리드/방사형) · 복제 개수 · 열·행 · 이동 X/Y · 칸 간격 X/Y · 반지름 · 시작 각도 · 각도 범위 · 바깥쪽 향하기 · 중심 · 회전 단계 · 크기 단계 · 끝 불투명도 · 랜덤 위치/회전/크기 · 시드.
- 피벗 = 입력 `max_result_rect` 의 중심(내용 경계 중심): 회전·크기는 각 클론 자신의 중심 기준, 선형은 원본 자리에서 출발, 그리드·방사형은 `중심` 파라미터 기준으로 배치.
- 출력 `max_result_rect` = 모든 클론으로 변환한 입력 경계의 합집합(+1px). 입력은 전체(±100000)를 요청 → AE 가 레이어 경계로 자름.
- 배치 모드별 항목 숨김: AE 는 `PF_PUI_INVISIBLE` 을 동적으로 못 바꾸므로 `AEGP_DynamicStreamSuite2::AEGP_SetDynamicStreamFlag(HIDDEN)` 을 `PF_Cmd_UPDATE_PARAMS_UI`/`USER_CHANGED_PARAM`(배치 popup 은 `PF_ParamFlag_SUPERVISE`) 에서 호출. 플러그인 ID 는 GlobalSetup 의 `AEGP_RegisterWithAEGP`. 그룹(topic) 을 안 쓴 이유: 스트림 인덱스 = 파라미터 인덱스 를 단순하게 유지.
- 실측(AE 2026, 100×100 사각형): 선형 4개·이동 150 → 중심 300/450/600/750, 크기 단계 −20% → 100/80/60/40 px 제자리 축소, 끝 불투명도 25% → 100/75/50/25%, 그리드 3×2 → 열 450/600/750 · 행 325/475, 방사형 r200 → 상하좌우 200px, 회전 45° → 대각선 폭 140, 애니메이션 소스 0.5 s → 클론 전부 같은 프레임. 1/100/300/1000 클론 렌더 ≈ 0/44/71/84 ms 증분.

## 겪은 함정
- `PF_REGISTER_EFFECT_EXT2` 매크로는 지역 변수 `result` 에 대입한다 — `return PF_REGISTER_EFFECT_EXT2(...)` 는 컴파일 오류.
- vcxproj 여러 개가 같은 `IntDir` 을 쓰면 MSB8028 경고 + 정리 오작동 → `out\obj\$(Configuration)\$(ProjectName)\`.
- 동적으로 숨긴 스트림은 **스크립트 `setValue` 가 실패**한다("property is hidden"). 스크립트로 `배치` 를 바꿔도 숨김 상태는 ECW 가 갱신될 때(UPDATE_PARAMS_UI) 반영되므로, 스크립트에서 값을 넣을 땐 현재 표시 중인 항목만 다룬다.
- `saveFrameToPng` 는 비동기 반환 → 렌더 시간은 PNG mtime 차이로 측정.
- 파라미터 이름은 **UTF-8 그대로** 넘기면 AE 2026 에서 한글이 정상 표시된다 (CP949 변환하면 깨짐).
- `PF_OutFlag`/`OutFlags2` 값은 PiPL(.r) 과 GlobalSetup 이 동일해야 한다 (Stroke: 0x02000200 / 0x08001480, Cloner: 0x06000200 / 0x08001480 — SEND_UPDATE_PARAMS_UI 추가).
- 경계 픽셀 판정: 전경은 dIn, 배경은 dOut 만 본다 — 자기 자신은 항상 씨앗(거리 0)이라 둘 다 보면 전부 경계로 오판.
- SDK 헤더에 CP949 로 표현 불가한 문자가 있어 `/utf-8` 없이는 C4819 → 오류.
