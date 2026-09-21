# Changelog

모든 릴리스는 [GitHub Releases](https://github.com/galaon/BANG_Toolbox/releases) 에서 내려받을 수 있습니다.
최신 버전 바로 받기: **[BANG_Toolbox.zip](https://github.com/galaon/BANG_Toolbox/releases/latest/download/BANG_Toolbox.zip)**

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
