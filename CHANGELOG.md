# Changelog

모든 릴리스는 [GitHub Releases](https://github.com/galaon/BANG_Toolbox/releases) 에서 내려받을 수 있습니다.
최신 버전 바로 받기: **[BANG_Toolbox.zip](https://github.com/galaon/BANG_Toolbox/releases/latest/download/BANG_Toolbox.zip)**

## [1.6.0] — 2026-09-26

### Added (BANG Stroke)
- **`Fill Gaps`** — 획이 글자 속 카운터(A 의 삼각형, g 의 고리)를 거의 메웠는데 가운데만 애매하게 남는 자리를 획 색으로 채웁니다.
  - `Off` / **`Narrow Gaps`**(`Gap Size` — 기본 24px — 보다 좁은 구멍만) / **`All Counters`**(큰 카운터까지 전부).
  - 글자 바깥의 배경은 건드리지 않고 닫힌 구멍만 골라 채웁니다.
  - 기존 `BANG Stroke` 인스턴스는 **그대로 씁니다** (파라미터를 맨 뒤에만 붙였습니다).

### Changed (BANG Gradient)
- **`Randomize` 가 ‘똑색’ 을 피합니다.**
  - 예전에는 OKLCh 값을 sRGB 로 바꿀 때 범위를 넘으면 **채널을 그냥 잘라냈고**, 그러면 색상이 틀어지고 채도가 빠져 탁해졌습니다.
    이제는 그 밝기에서 sRGB 안에 들어오는 최대 채도를 먼저 구해 그것의 58~100% 로만 고릅니다(gamut-relative saturation).
  - **어두운 주황·노랑이 곷 갈색·올리브** 이므로, OKLCh 색상 88° 주변 띄에서만 밝기 바닥을 최대 0.86 까지 올렸습니다.
    짙은 남색·버건디·포레스트그린은 그대로 나옵니다. (실측: 청동색 `172,126,40` → 호박색 `249,194,98`)
  - 색상 배치도 유사색·**넓은 스윙**·보색·분할보색·삼색·단색조 여섯 가지에서 고르므로 색상폭은 오히려 넓어졌습니다.
- **정지점이 `Stop 1`…`Stop 8` 접힐 그룹으로 묶였습니다.** Color·Position·Opacity 가 줄 하나로 줄어들어,
  `Randomize` 로 정지점 갯수가 바뀔 때 이펙트 컨트롤 창이 크게 밀리지 않습니다(최대 24줄 → 8줄).
- ⚠ 파라미터 순서가 바뀌었습니다 — 이전 버전으로 만든 `BANG Gradient` 인스턴스는 **다시 적용**해야 합니다.

## [1.5.1] — 2026-09-26

### Changed (BANG Gradient)
- **`Alpha` 기본값이 `Replace (opacity cuts out)`** 로 바뀌었습니다. 새로 적용하면 정지점 불투명도가 곧바로 레이어 알파를 뚫습니다.
- **Fit 버튼이 누를 때마다 방향을 뒤집습니다** — `Fit Horizontal` 은 좌→우 ↔ 우→좌, `Fit Vertical` 은 위→아래 ↔ 아래→위.

### Added (BANG Gradient)
- **`Fit Diagonal` 버튼** — 누를 때마다 사분면을 시계방향으로 돌립니다(↘ → ↙ → ↖ → ↗). 대각선에서도 내용의 모서리에 정확히 붙고, `Lock Gradient` 를 켜면 그대로 따라갑니다.

## [1.5.0] — 2026-09-26

### Added (BANG Gradient)
- **Fit 을 세 개로 나눔** — `Fit Horizontal` · `Fit Vertical` 버튼과 `Lock Gradient` 토글. 이전의 `Fit Keeps Following` 체크박스는 Lock 이 대신합니다.
  - 가로/세로를 각각 누르면 내용의 좌·우 또는 최상단·최하단 가운데로 붙고, 가운데 기준 모양(Radial 등)은 중심→꼭지점입니다.
  - `Lock Gradient` 를 켜면 표현식으로 고정돼 크기·위치 변화를 따라가고, 끄면 그 시점 값으로 굴려 넣습니다.
- **바를 더블클릭하면 그 자리에 정지점 추가** — 포토샵·AE 그라데이션 편집기처럼. 색과 불투명도는 그 지점의 값을 그대로 받아옵니다.
- **⇄ 좌우 반전 버튼** — 바 오른쪽 끝에. `Reverse` 체크박스와 달리 정지점 위치 값 자체를 뒤집어 계속 편집할 수 있습니다.
- **프리셋** — `Black to White` · `White to Black` · **`Chrome`**(금속 반사) · `Gold` · `Sunset` · `Ocean` · `Fire` · `Rainbow` · `Fade Out`.
- **`Randomize`** — OKLCh 색상환에서 골라 밝기·채도가 자연스러운 조합을 만듭니다(RGB 난수처럼 탁해지지 않습니다).
- **Import / Export** — **`.css`** · `.ggr`(GIMP) · `.json`.
  - `.css` 는 colorffy·coolors 가 공유하는 그 형태 그대로: `background: linear-gradient(180deg in oklab, rgba(...) 0.0%, …);` — 각도와 보간 색공간까지 함께 오갑니다. 가져올 때는 `rgba()`·`rgb()`·`#hex`(8자리 알파 포함) 를 모두 읽습니다.
  - `.ggr` 는 문서화된 순수 텍스트 포맷이라 GIMP·Krita·Inkscape 가 그대로 읽습니다. (Photoshop `.grd` 는 비공개 바이너리라 제외)
- **`Alpha` — `Replace (opacity cuts out)`** — 정지점 불투명도가 원본 위에 얹혀지는 대신 **레이어 알파를 그대로 뚫습니다**. 0% 인 자리는 원래 색이 비치는 게 아니라 투명해집니다.

### Changed (BANG Gradient)
- `Color Stops` → **`Gradient Colors`** 로 이름을 바꾸고, `Stops` 개수 슬라이더도 그 안으로 넣었습니다.
- **바와 칩이 불투명도를 보여줍니다** — 0% 에 가까울수록 체크무늬가 드러나도록 그렸습니다.
- ⚠ 파라미터 순서가 바뀜 이전 버전으로 만든 `BANG Gradient` 인스턴스는 **다시 적용**해야 합니다.

## [1.4.1] — 2026-09-26

### Added (BANG Gradient)
- **`Fit to Layer` 버튼 + `Fit Keeps Following` 체크박스** — Start·End 를 레이어 내용 크기에 딱 맞춥니다.
  - 방향은 지금 Start→End 방향을 그대로 쓰므로, 세로 그라데이션이면 **최상단 가운데 → 최하단 가운데**, 가로면 좌·우 가운데로 붙습니다.
  - 기준점이 가운데인 모양(Radial·Angular·Diamond·Reflected)은 **Start = 중심, End = 꼭지점**. Contour 는 짧은 변의 절반을 Span 으로.
  - 체크박스를 켜고 누르면 값 대신 **표현식**을 걸어, 내용 크기나 위치가 바뀜어도 알아서 따라갑니다. 회전·스케일은 이펙트 다음에 적용되므로 그라데이션이 도형과 함께 그대로 돌아갑니다.
- **가로 색 띄(Stops Bar)** — Color Stops 맨 위에 그라데이션 미리보기와 정지점 칩을 **가로로** 다시 그렸습니다. 칩을 클릭하면 그 정지점의 색 선택기가 바로 뜨고, 위치도 실제 비율대로 놓입니다.

### Changed / Fixed (BANG Gradient)
- **Angular·Repeat 의 계단 현상** — t 가 1→0 으로 뚝 끊기는 이음매는 한 픽셀 안에서 색이 통째로 바뀝니다. 이제 그 줄에 닿는 픽셀만 골라 **4×4 서브샘플**로 썽니다(비용은 이음매 줄에만).
- **안 쓰는 정지점은 숨김** — `Stops` 개수보다 뒤에 있는 색·위치·불투명도 줄은 회색이 아니라 목록에서 아예 사라집니다.

## [1.4.0] — 2026-09-26

### Added — 새 네이티브 이펙트 **BANG Gradient**
AE 기본 Ramp 는 색이 둘뿐이고 sRGB 로만 섞이며 띄는 Ramp Scatter 로만 가립니다. 그 빈틈을 채운 이펙트입니다.
- **정지점 최대 8개** — 각각 색·위치·불투명도. `Stops` 를 바꾸면 위치가 고르게 자동 재배치되고, 안 쓰는 정지점은 회색으로 숨습니다.
- **모양 6가지** — `Linear` / `Radial` / `Angular`(원뿔) / `Diamond` / `Reflected` / **`Contour`**. Contour 는 알파 경계까지의 거리를 따라 칠하므로 글자·도형 윤곽을 따라 흐르는 그라데이션을 만듭니다(BANG Stroke 와 같은 거리장).
- **보간 색공간** — `sRGB` / `Linear` / **`OKLab`**(중간에서 밝기가 꾸지지 않음, 기본값) / `OKLCh`(색상환을 돌아 무지개처럼 — 짧은 길·긴 길).
- **반복** — `Clamp/Repeat/Mirror` + `Cycles`·`Phase`·`Reverse`, `Smoothness`(정지점 사이 이징).
- **Dither** — 8bpc 로 떨어질 때 생기는 띄를 없애는 미세 잡음(기본 40%).
- **원본과 합성** — `Blend With Original`(Normal/Multiply/Screen/Add/Overlay) + `Amount`, `Preserve Alpha`(레이어 알파 안에서만).
- 8/16/32bpc · SmartFX · 멀티프레임 렌더. 패널 Tools 에 `Gradient` 타일 추가.

### Changed (BANG Stroke)
- 파라미터 순서를 **Width → Position → Corner → Miter Limit → Offset → …** 로 바꿨습니다(자주 만지는 것을 위로).
- `Fill` 이 **Solid 이면 `Gradient` 그룹이 회색으로 접힙니다**. Gradient 로 바꾸면 바로 펼쳐집니다.
- ⚠ 파라미터 순서가 바뀜 이전 버전으로 만든 `BANG Stroke` 인스턴스는 **다시 적용**해야 합니다.

## [1.3.11] — 2026-09-25

### Fixed (BANG Stroke — 모서리 품질·안정성)
- **크래시** — 1.3.10 최적화에서 들어간 버퍼 밖 읽기. 법선을 계산할 때 내용 상자의 마지막 행/열에서 `i+w` 를 읽어 AE 가 *"BANG Stroke가 After Effects 종료를 유발했을 수 있습니다"* 와 함께 죽을 수 있었습니다(큰 텍스트 · Corner ≠ Round).
- **뿔처럼 튀어나가는 획** — 변을 하나밖에 못 찾은 경우에도 보정을 적용해 Miter Limit 이 적용되지 않았습니다. 이제 **서로 다른 변 둘**을 찾은 경우에만 손대고, 뻗음은 어떤 경우도 Limit 를 넘지 못합니다.
- **획에 파이는 가는 줄** — 거의 같은 방향의 두 변이 잡힐 때 베벨 현 식의 1/cos 가 발산해 획을 잘랐습니다. 날카로운 각은 현 대신 한계치로 자르고, 잘리는 깊이도 제한합니다.
- **외곽의 1px 계단** — 보정을 **꼭지점의 부채꼴 안**으로 제한했습니다. 예전엔 꼭지점 근처라면 그냥 변 옆인 픽셀까지 평면 값으로 덮어써, 평면 오차만큼 외곽에 들첩거림이 생겼습니다.
- **곡선을 꼭지점으로 오인** — 꼭지점 판정을 30° → 45° 로 올려 반지름 3~4px 짜리 곱선이 꼭지점으로 잡히는 일을 없엠습니다.

직선 구간 획 폭·Position 세 가지·회전 4각의 마이터 꼭지점(이론값 28.28px 대비 27.75~28.75)·十자 도형의 볼록·오목 모서리는 전부 그대로입니다.

## [1.3.10] — 2026-09-25

### Performance (BANG Stroke v1.5 — Miter 최적화)
1440×2560 텍스트 레이어 · 획 60px 기준 거리장 계산 시간(프레임당):

| Corner | 이전 | 지금 |
|---|---|---|
| Round | 19.3 ms | **10.1 ms** |
| Miter | 291.0 ms | **37.0 ms** (7.9×) |
| Bevel | 50.4 ms | **26.8 ms** |

- **거리장 격자를 꼭 필요한 만큼만** — 예전엔 출력 영역을 여백(margin)만큼 사방으로 넓혔는데, Miter 는 그 여백이 `Miter Limit` 배라 격자가 2.5배까지 커졌습니다. 씨앗(전경 픽셀)은 어차피 입력 영역 안에만 있으므로 이제 입력∪출력 영역만 씁니다.
- **획이 닿지 않는 쪽 거리장은 아예 계산하지 않음** — Outside 획이면 안쪽 거리장(EDT + 모서리 보정)을 통째 건너뜁니다. Round 도 같이 빨라졌습니다.
- **모서리 평면을 꼭지점당 한 번만 수집** — 같은 변에서 나온 평면은 평균내서 보통 2~4개로 줍니다. 예전엔 띄 픽셀마다 17×17 창을 두 번씩 훑어 이 단계에만 175 ms 가 들었고, 지금은 3 ms 입니다.
- 꼭지점 인접 여부를 미리 계산해 띄 픽셀당 조회 한 번으로 끝내고, 법선·꼭지점 계산은 레이어 내용 상자 안으로 제한했습니다.

렌더 결과는 바뀜지 않았습니다 — 회전 0°/10°/22.5°/45° 마이터 꼭지점, Outside/Center/Inside 획 폭, 十자 도형의 볼록·오목 모서리, 그라데이션 불투명도 램프 모두 최적화 전과 동일한 픽셀값을 확인했습니다.

### Added (개발)
- `native/build-native.ps1 -Defines BANG_FX_LOG` — 단계별 소요 시간을 `%TEMP%\bang_stroke.log` 에 기록하는 프로파일링 빌드.

## [1.3.9] — 2026-09-25

### Added (BANG Stroke v1.4)
- **모서리 모양 `Corner`** — `Round`(기본, 지금까지와 동일) / **`Miter`**(볼록 꼭지점을 각지게 뀌족하게) / **`Bevel`**(꼭지점을 짧게 잘라냄). `Miter Limit`(1~10, 기본 4)을 넘어설 날카로운 각은 자동으로 Bevel 로 대체됩니다.
  - 안티에일리어싱된 알파의 기울기로 인접한 두 변의 법선을 복원해 계산합니다 — 도형을 회전시켜도 모서리 각도가 동일하게 나오고, 직선 구간의 획 두께는 그대로 유지됩니다(기울어진 변이 두꺼지지 않고, 오목한 모서리도 깎이지 않음).
  - 실측(200px 사각형 · 획 20px): 회전 0°/10°/22.5°/45° 에서 마이터 꼭지점이 28.75 / 28.50 / 29.00 / 29.00 px — 이론값 28.28 px 대비 모두 ±0.7px 이내.
- **그라데이션 불투명도** — `Gradient` 그룹에 `Opacity A` / `Opacity B`. 색과 또같이 불투명도도 보간되어 획이 서서히 사라지게 만들 수 있습니다(예: Across Stroke + Opacity B 0 → 바깥으로 페이드).

### Changed
- 파라미터가 추가되어 v1.3 으로 만든 `BANG Stroke` 인스턴스는 **다시 적용**해야 합니다.
- `Miter` 사용 시 출력 버퍼 여백을 `Miter Limit` 배만큼 더 확보합니다(뀌족한 모서리가 잘리지 않도록).

## [1.3.8] — 2026-09-25

### Changed (BANG Stroke v1.3 — 구조 변경)
- **획 하나 = 이펙트 하나.** `Stroke 1/2/3` 그룹과 `Enable` 체크박스를 없았습니다. 여러 겹은 **`BANG Stroke` 를 두 번 이상 적용**해서 만듭니다 — 두 번째 인스턴스는 첫 획이 포함된 알파를 입력으로 받으므로 자연히 **첫 획 바깥에** 그려집니다. 겹마다 두께·색·그라데이션·Softness·Blend 를 완전히 따로 줍니다.
  - 이유: 그룹 전체를 회색처리하면 그 안의 `Enable` 까지 함께 비활성화되어 **Stroke 2/3 을 켜지 못하는 문제**가 있었습니다.
- 파라미터는 평평하게: `Position`·`Width (px)`·`Offset (px)`·`Softness (px)`·`Opacity`·`Blend`·`Fill`·`Color` + `Gradient`·`Edge Noise`·`Body` 그룹. 그라데이션·Edge Noise·Body Opacity 등 v1.2 에서 추가된 기능은 그대로 유지.
- v1.2 로 만든 `BANG Stroke` 인스턴스는 파라미터 구성이 바뀌었으므로 **다시 적용**해야 합니다.

## [1.3.7] — 2026-09-23

### Added (BANG Stroke v1.2 — 기능 확장)
- **획 3겹** — `Stroke 1/2/3` 그룹. 각각 `Enable`·`Position`(Outside/Center/Inside)·`Width`·`Offset`·`Softness`·`Opacity`·`Blend`(Normal/Multiply/Screen/Add)·`Fill`. 꺼진 그룹은 접힌 채 회색.
- **그라데이션 채우기** — `Fill = Gradient` 시 `Color B`·`Gradient Type`(**Across Stroke** 획을 가로지르며 / **Linear** 각도 / **Radial** 내용 중심)·`Gradient Angle`·`Gradient Scale (px)`·`Reverse`.
- **Edge Noise** — 거리장에 fBm 값 노이즈를 더해 가장자리를 거칠게: `Amount (px)`·`Scale (px)`·`Detail`(옥타브 1~5)·`Evolution`(각도, 키프레임으로 흐름)·`Seed`. 모든 획이 같은 윤곽으로 함께 흔들린다.
- **Body Opacity** — 본체만 반투명하게(획은 그대로). 기존 `Body`(Keep/Hide)·`Order` 는 유지.

### Changed
- 기존 단일 획 파라미터는 `Stroke 1` 그룹으로 이동 — v1.1 로 만든 이펙트 인스턴스는 **다시 적용**해야 합니다.

## [1.3.6] — 2026-09-22

### Added
- **BANG Cloner v1.7 — Path 를 셰이프 레이어로** — `Path Layer` 에서 컴프 안 **셰이프 레이어를 직접 선택**(펜 패스·사각형(둥근 모서리)·타원, 그룹 변환·레이어 위치/회전/크기 반영, 패스 여러 개면 이어서). Path Layer 가 없으면 `Mask Path` 폴백. 애니메이션: `Start`/`End`(배치 구간), `Offset`(키프레임), **`Speed (%/s)`**(키프레임 없이 흐름, 음수 = 역방향), `Reverse`, `Loop`(끄면 끝에 멈춤), `Align to Path` + `Align Angle`.
- **BANG Cloner — `Random Opacity`**, **`Bake to Layers` 버튼**(이펙트 맨 아래): 현재 프레임의 클론을 실제 레이어(복제본, 이펙트 제거)로 굳히고 원본은 숨김. 실행 취소 1회로 복원.
- **Bento Grid** — 셰이프·텍스트 레이어도 배치(내용 경계 기준). 이전엔 "not a visual AV layer" 로 건너뛰던 문제.

### Changed
- Cloner 타일: 레이어 + 셰이프 레이어 선택 시 `Path Layer` 를 설정(마스크 복사 방식 폐기). 두 셰이프 레이어면 채우기 없는 쪽(없으면 펜 패스 쪽)이 경로.
- 패널 카드 간격 축소, Bento 아이콘 교체.
- 네이티브: PreRender 에서 AEGP 를 쓰므로 MFR(`SUPPORTS_THREADED_RENDERING`) 플래그 해제(렌더 내부 멀티스레드는 유지), `NON_PARAM_VARY`(Speed).

## [1.3.5] — 2026-09-22

### Added
- **BANG Cloner v1.6 — `Path` 배치** — 이 레이어의 **마스크 패스**를 따라 길이 기준으로 균등 배치(`Mask Path`, `Align to Path`, `Path Offset %`). 패널의 Cloner 타일은 **레이어 + 셰이프 레이어(펜 패스)** 를 함께 선택하고 누르면 셰이프의 패스(그룹 변환·레이어 변환 반영)를 대상 레이어의 `BANG Path` 마스크로 복사하고 Layout 을 Path 로 바꿉니다. (사각형·타원 파라메트릭 셰이프는 "베지어 패스로 변환" 후 사용)

### Changed
- **BANG Cloner** — Radial 에 `Center on Object`(기본 켜짐: 원 중심 = 오브젝트 중심, 끄면 `Center` 포인트 사용). 퀵 버튼 이름 `Start Angle Preset` / `Sweep Preset` / `Rotation Step Preset`, Sweep 도 증감 프리셋(가운데 = 360 리셋). 그룹 이름 오른쪽에 실선(`Linear ────────`) 구분.
- **Bento Grid 창** — 한글화·정리(사용법 4줄, 크기 / 배치 / 옵션 패널, 툴팁), 눈에 띄는 **`▶ Bento Grid 적용`** 버튼, `무작위 배치` / `Bento 마스크 제거` / `선택 레이어의 마스크 전부 삭제…`.
- `reload-in-ae.ps1` 이 재시작 후 AE 캐시를 비움(같은 버전 `.aex` 교체 시 예전 렌더가 캐시에서 나오던 문제).

## [1.3.4] — 2026-09-22

### Changed
- **패널 세로 압축** — Layout 을 한 줄 `[Anchor 3×3] [Align 3×3] [Null / Align to 세로 스택]` 으로, 카드 제목(LAYOUT · COLOR · TOOLS) 제거, Align to 는 라벨·가로 트랙·현재 값만 표시(좁으면 라벨 생략).
- **Bento Grid** — 접이식 카드를 없애고 Tools 의 정사각 타일로. 클릭하면 동봉한 **원본 `BentoGrid.jsx` 팔레트 창**이 열립니다(이미 열려 있으면 앞으로).
- **Align** — 선택에 3D 레이어가 없으면 측정용 프로브를 2D 로 만들어 3D 축 표시가 스쳐 지나가지 않음(있을 때만 3D 프로브).
- **BANG Cloner v1.5** — 새로 적용하면 `Linear` 만 펼쳐지고 `Grid`·`Radial`·`Step`·`Random` 은 접힌 채 시작. Step 글리프 `↻` → `∆`.

## [1.3.3] — 2026-09-22

### Changed
- **Layout 카드 재배치** — 1행 `[Anchor 3×3] [Align 3×3]`, 2행 `[Null] [Align to]`(가로 스위치). 폭에 맞춰 두 열이 같이 늘고(상한 9.5rem) 좁아지면 `Align to` 라벨만 생략. 이전의 브레이크포인트 방식 제거.
- **Tools** — `Quote / Align`, `Crop / Precomp` 두 줄 라벨. `All`·`자동` 토글 제거: 전체 프레임 샘플링은 프리컴프 안 `BANG Crop` 컨트롤러의 **`All Frames` 체크박스**로, 스크립트 클로너 자동 갱신은 항상 켜짐.
- **Color** — 복사 시 미리보기 위에 "복사 완료!" 토스트(1.2초). 피커 아이콘을 mingcute `color-picker-fill` 로 교체.
- **BANG Cloner v1.4 (Effect Controls)** — `Origin Preset` 3×3 아이콘을 Anchor 컨트롤과 같은 굵은 화살표 + 가운데 Align Center 아이콘으로(Drawbot 패스). 퀵 버튼 22px 정사각. `Nudge` 한 줄 11개(`-360 -90 -15 -2.5 -1 0 +1 +2.5 +15 +90 +360`). 그룹 이름에 글리프(`⋯ Linear` `▦ Grid` `◎ Radial` `↻ Step` `⚄ Random`), 선택되지 않은 배치 그룹은 접힌 채 **회색(비활성)** 으로 표시.

## [1.3.2] — 2026-09-22

### Changed
- **BANG Cloner v1.3 (Effect Controls)** — `Origin Preset` 를 **3×3** 로 배치. 각도 퀵 버튼은 절대값 대신 **증감(Nudge)**: `-360 -180 -90 -45 -15 -2.5 -1 | 0 | +1 +2.5 +15 +45 +90 +180 +360` (0 만 0 으로 리셋; `Start Angle`·`Rotation Step`). `Sweep` 은 절대 프리셋 유지. px/deg 단위를 이름에 표기(`Gap (px)`, `Radius (px)`, `Sweep (deg)` …).
- **Crop Precomp** — 패널의 `Pad` 입력을 없애고, 자를 때 프리컴프 안에 **`BANG Crop` 컨트롤러**(shy Null + `Pad (px)` 슬라이더)를 만듭니다. 그 값을 바꾸면 약 1초 뒤 자동으로 다시 잘립니다(선택한 프리컴프 레이어 또는 열려 있는 프리컴프 기준). 타일 이름 `Quote Align`, `Crop Precomp`.
- **패널 반응형** — 폭 340px 미만이면 Layout 카드가 `[Null ↑ Anchor] [Align to ↑ Align]` 두 스택으로 바뀌고 Align to 스위치가 가로형이 됩니다(FHD 좁은 도킹 대응). Anchor/Align 중앙 버튼의 밝기 강조 제거.
- **설치 권한** — `tools/grant-write-access.ps1` 를 한 번 실행하면(UAC 1회) 이후 `deploy-local.ps1` / `build-native.ps1 -Install` 이 UAC 없이 복사합니다. 두 스크립트는 쓰기 권한이 있으면 승격하지 않고, 설치 후 해시로 검증합니다.

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
