# Changelog

모든 릴리스는 [GitHub Releases](https://github.com/galaon/BANG_Toolbox/releases) 에서 내려받을 수 있습니다.
최신 버전 바로 받기: **[BANG_Toolbox.zip](https://github.com/galaon/BANG_Toolbox/releases/latest/download/BANG_Toolbox.zip)**

## [1.3.1] — 2026-09-22

### Changed (BANG Cloner v1.2 — Effect Controls 정리)
- **그룹**: 파라미터를 `Linear` / `Grid` / `Radial` / `Step` / `Random` 접이식 그룹으로 정리. 배치(Layout)에 맞는 그룹만 자동으로 펼쳐지고 나머지는 접힘. Grid 에선 `Count` 숨김.
- **퀵 버튼**: `Start Angle`·`Sweep`·`Rotation Step` 아래 `Preset` 버튼 줄(0·15·30·45·60·90·120·180 / 45·90·180·270·360) — 클릭 한 번으로 값 설정, 현재 값은 강조 표시.
- **Grid Origin → `Origin X` / `Origin Y`** 슬라이더(1 기준, `Columns`/`Rows` 를 넘으면 자동 보정) + `Origin Preset` 9방향 버튼(↖ ↑ ↗ ← ● → ↙ ↓ ↘, ● = Center).
- 회전/크기 단계는 **원본 클론을 0 으로** 앞뒤로 누적(이전엔 항상 첫 클론 기준).

## [1.3.0] — 2026-09-22

### Added
- **BANG Cloner (네이티브 이펙트, Windows)** — 레이어를 복제하지 않고 소스의 현재 프레임을 N개 인스턴스로 렌더하는 클로너(Motion Tile 모델). Layout Linear/Grid/Radial. 간격은 소스 크기와 무관한 **Gap(이웃 경계 사이 px, 음수 = 겹침)**, Linear 는 `Origin Index`(원본이 몇 번째인지)·`Direction`·`Offset`, Grid 는 `Columns/Rows`·`Gap X/Y`·`Grid Origin`(원본이 놓이는 칸 9방향), Radial 은 `Radius`·`Start Angle`·`Sweep`·`Face Outward`·`Center`. 공통 `Rotation/Scale Step`·`End Opacity`·`Random Position/Rotation/Scale`·`Seed`. 배치에 맞지 않는 항목은 Effect Controls 에서 자동 숨김. 8/16/32bpc, 클론 경계만큼 버퍼 확장, 멀티스레드 + 정수 이동 직접 복사 경로. 소스 애니메이션 타이밍이 클론 개수와 무관하게 정확(실측: 1000 클론 100×100 ≈ 0.1 s, 1440×2560 20 클론 회전 ≈ 0.2 s). 파라미터 이름 영문. `plugins/BANG_Cloner.aex`.
- **패널 Stroke 타일** — 선택 레이어에 `BANG Stroke` 적용.
- **BANG Stroke (네이티브 이펙트, Windows)** — 알파 경계 거리 기반 획: `Position`(Outside/Center/Inside)·`Width`·`Offset`·`Color`·`Opacity`·`Softness`·`Body`(Hide = 획만)·`Order`. 파라미터 이름 영문. 8/16/32bpc, 레이어 경계 밖 획도 잘리지 않음. `plugins/BANG_Stroke.aex` 를 AE `Plug-ins\BANG\` 에 복사해 설치.
- **Bento Grid** — 사용자의 `BentoGrid.jsx` v1.3.0 엔진 이식(타일 크기 배정·빔/그리디 패킹·Cover 크롭 마스크·마스크 정리). Unit/Gap/Width, Fit, Variety, Packing Style, Crop/Center/Mix, Repack·Randomize·Clear Masks.
- **Cloner 래스터라이즈** — 클론을 독립 레이어로 굳힘(표현식 제거, shy/잠금 해제, 이펙트 정리). 이펙트의 체크박스로 실행.

### Changed
- **Precomp Fit → Precomp Crop** — 이름 변경. 셰이프/텍스트 Stroke 두께를 경계에 포함(가장자리 잘림 해결). Pad 값을 바꾸면 자동 재실행, 마우스 휠로 ±1(Shift ±10), `px` 단위 표시.
- **Cloner 타일** — 네이티브 `BANG Cloner` 가 설치돼 있으면 그것을 적용하고, 없으면 기존 스크립트 클로너(`BANG 클로너` Pseudo Effect + 레이어 복제)로 동작. `자동` 토글은 스크립트 클로너 전용.
- **Cloner(스크립트)** — 소스 레이어의 Scale 을 바꾸면 클론 간격·반지름도 같은 비율로 커져 전체가 하나처럼 스케일됨. `자동` 토글(기본 켜짐): 소스/클론 선택 중 이펙트의 `복제 개수`가 바뀌면 약 1초 뒤 자동 갱신(래스터라이즈 체크도 자동 실행).
- **Bento Grid** — 별도 접이식 카드로 분리, 한글 UI(크기 → 배치 → 실행 순), 옵션 툴팁.
- **Align to 스위치** — 선택된 값을 강조색으로 표시. Anchor 중앙 버튼에 앵커 아이콘 표시(이전엔 CSS 가 fill 을 지워 보이지 않던 문제 수정).
- **Cloner** — 여러 개별 컨트롤 대신 **단일 Pseudo Effect `BANG 클로너`**(한글 항목명, `jsx/BANG_Cloner.ffx` 동봉)가 유일한 설정처. 패널은 `Cloner — 적용 / 갱신` 버튼 하나: 이펙트 없으면 적용+복제, 있으면 `복제 개수`(1 = 클론 제거)·`배치 모드`로 갱신, `실행 > 래스터라이즈` 체크 후 누르면 독립 레이어화. 클론은 shy+잠금으로 숨김. 소스/클론 어느 쪽을 선택해도 동작.
- **패널 재구성** — 카드 4개: Layout(Null·Anchor·Align·Align to) / Color / Tools(Quote·Crop·Cloner 정사각 타일 + 옵션) / Bento Grid(접이식). Anchor·Align 그리드를 같은 규격(둥근 셀·동일 아이콘 크기·중앙 강조·hover 동일)으로 통일, Align to 도 박스 처리, 세로 스위치 중앙 정렬. Precomp Crop 아이콘 교체(boxicons crop).
- **Layout 한 줄** — Null · Anchor Point(mdi thick 화살표 + 앵커 아이콘) · Align 3×3(가로중앙·상·세로중앙 / 좌·양축중앙·우 / 가로분배·하·세로분배) · 세로 `Selection ⇅ Comp` 스위치를 한 줄에 배치. Z 축 버튼 제거(내부 함수는 유지).
- **Quote Align** — 사용자 제작 아이콘(v3)으로 교체.

## [1.2.0] — 2026-09-21

### Added
- **Align 3D** — 3D 레이어가 켜져 있어도 동작하는 정렬/분배. 좌·중·우 / 상·중·하, 분배 X·Y, `Z` 토글 시 Front·Mid·Back·분배 Z. 기준: 선택 영역(기본) / `Comp`. 부모·회전·스케일이 있어도 월드 공간 경계 기준으로 이동.
- **Precomp Fit** — 프리컴프 크기를 내부 레이어 경계(+`Pad` 여백)에 맞춤. 부모 컴프에서 프리컴프 레이어를 선택해 실행하거나 프리컴프 안에서 실행. 모든 인스턴스의 앵커를 보정해 시각적 위치 불변. `All` 토글로 워크에어리어 전체 프레임 샘플링.
- **Cloner** — Cinema 4D Cloner 를 참고한 라이브 복제. 소스 레이어에 `Cloner Mode / Count / Offset / Grid Columns·Spacing / Radius / Start·End Angle / Align to Radius / Step Rotation·Scale·Opacity / Random Seed·Position·Rotation·Scale` 컨트롤을 삽입하고, 클론은 표현식으로 실시간 재배치(Linear · Grid · Radial). 버튼 재클릭 = `Cloner Count` 값으로 재복제, `×` = 클론 제거.

### Changed
- **Green Null** — Null 을 컴프 최상단이 아니라 **선택 레이어 중 최상단 바로 위**에 생성. 위치는 선택 레이어들의 **오브젝트 중심**(마스크가 있으면 마스크 영역 중심)의 평균.
- **Quote Align** — 변환 시 원본 포인트 텍스트를 `[OLD]` 로 남기지 않고 같은 이름의 박스 레이어로 대체 (Ctrl+Z 한 번으로 복원).
- Quote Align 아이콘을 Phosphor `quotes` 로 교체.

### Accessibility / UI
- 키보드 포커스 링(`:focus-visible`), `prefers-reduced-motion` 지원, 아이콘 전용 버튼 `aria-label`, 장식 SVG `aria-hidden`.

### Dev
- `tools/` — 패키징(`build.ps1`), 로컬 배포(`deploy-local.ps1`), CEP 원격 디버깅 하네스(`cep-eval.js`, `cep-shot.js`), 기능별 검증 씬(`test_scene_*.jsx`). `.debug` 로 원격 디버깅(8089) 활성화(패키지 미포함).

## [1.1.0] — 2026-09-21

### Added
- **Quote Align (따옴표 정렬)** — 인용 자막의 여는 따옴표를 본문 정렬선 밖으로 내어쓰기(행잉 펑추에이션). 박스 텍스트는 세트 적용, 포인트 텍스트는 박스 텍스트로 자동 변환(본문 첫 글자 위치 유지). 겹·홑따옴표 모두 지원. **AE 2024 (24.3) 이상 필요.**

### Fixed
- ExtendScript 한글 메시지 인코딩(UTF‑8 BOM).

## [1.0.0] — 2026-06-23

- 최초 배포: Green Null · Anchor Point 9방향 · Color Picker.
