# BANG_Toolbox

>create Null · Null 생성<br>
>move Anchor Point · 앵커포인트 이동<br>
>Align 3D · 3D 정렬<br>
>Precomp Crop · 프리컴프 크기 맞춤<br>
>Cloner · 복제 (네이티브 인스턴스 렌더)<br>
>Stroke · 알파 경계 획 (네이티브)<br>
>Bento Grid · 모듈 그리드 패킹<br>
>Quote Align · 따옴표 정렬<br>
>Color Picker · 색상선택<br>

[![Latest release](https://img.shields.io/github/v/release/galaon/BANG_Toolbox?label=latest&color=0099FF)](https://github.com/galaon/BANG_Toolbox/releases/latest)

## [즉시 다운로드](https://github.com/galaon/BANG_Toolbox/releases/latest/download/BANG_Toolbox.zip)

압축을 풀고 `INSTALL.txt` 의 순서대로 진행하십시오.
변경 내역은 **[CHANGELOG.md](CHANGELOG.md)** · 모든 버전은 [Releases](https://github.com/galaon/BANG_Toolbox/releases) 참고.



## ✨ 기능

- **Green Null**
  — 선택한 레이어에 Null을 만들고 자동으로 Parents 연결. Null은 **선택 레이어 중 최상단 바로 위**에 생성됩니다.
  — 위치는 선택 레이어들의 **오브젝트 중심**(마스크가 있으면 **마스크 영역 중심**)의 평균점.
- **Anchor Point (9방향)** — 3×3 버튼으로 앵커포인트를 이동. 시각적 위치는 그대로 유지되도록
  Scale·Z회전을 반영해 Position을 자동 보정하며, 마스크가 있으면 **마스크 영역 기준**으로 정렬합니다.
- **Color Picker** — 자체 피커입니다. 채도·명도 사각형 + 색상·불투명도 슬라이더, `HEX`/`RGB`/`HSB`/**`OKLCH`** 입력(감마 밖이면 채널을 자르지 않고 채도만 줄여 맞춥니다), 색상 히스토리, `#hex`·`rgb()`·`oklch()`·`AE [r,g,b,1]` 형식 복사.
  **화면 스포이드**는 확대 루페가 달린 전체화면 오버레이(`bin/BANG_Picker.exe`, Windows)로 AE 밖에서도 집을 수 있고, 집는 즉시 히스토리에 쌓이며 클립보드로 복사됩니다.
  `Apply` 는 고른 색을 선택한 텍스트·셰이프·솔리드·이펙트 Color 파라미터에 넣고, `Read` 는 그 반대입니다.
- **Crop Precomp** — 프리컴프 크기를 내부 레이어 경계(Stroke 포함)에 맞게 자릅니다. 부모 컴프에서 프리컴프 레이어를 선택해
  실행하거나 프리컴프 안에서 실행. 모든 인스턴스의 앵커를 보정해 **시각적 위치가 변하지 않습니다**. 여백과 전체 프레임 샘플링은 프리컴프 안에 생기는 **`BANG Crop` 컨트롤러(`Pad (px)`, `All Frames`)** 로 조절 — 값을 바꾸면 자동으로 다시 잘립니다.
- **Cloner (네이티브 이펙트 `BANG Cloner`)** — Cinema 4D Cloner 를 참고한 인스턴스 클로너. 레이어를 복제하지 않고 소스의 현재 프레임을 N개 변환·합성하므로 **개수와 무관하게 애니메이션 타이밍이 정확**하고 가볍습니다(1000개 ≈ 0.1 s). Layout Linear/Grid/Radial/**Path**(`Path Layer` 로 컴프 안 셰이프 레이어 선택 — 펜·사각형·타원, `Speed`로 흐르는 애니메이션·`Reverse`·`Loop`·`Start/End`; 레이어와 셰이프를 함께 선택해 타일을 누르면 자동 설정) (그룹별 정리, 배치에 맞는 그룹만 펼쳐짐) · 간격은 소스 크기와 무관한 **Gap**(이웃 경계 사이 px, 음수 = 겹침) · Linear `Origin Index`(원본이 몇 번째인지)·`Direction`·`Offset` · Grid `Columns/Rows`·`Gap X/Y`·`Origin X/Y` + 9방향 `Origin Preset` 버튼 · Radial `Radius`·`Start Angle`·`Sweep`·`Face Outward`·`Center on Object`/`Center` · Step `Rotation/Scale Step`·`End Opacity` · Random `Seed`·`Random Position/Rotation/Scale/Opacity` · **`Bake to Layers`**(클론을 실제 레이어로). 각도 항목 아래 **Nudge 퀵 버튼** 한 줄(−360 −90 −15 −2.5 −1 · 0 · +1 +2.5 +15 +90 +360, 0 = 리셋), Sweep 프리셋(45·90·180·270·360), Origin 3×3 화살표 버튼. `plugins/BANG_Cloner.aex` 설치 필요(INSTALL.txt 2‑1) — 미설치 시 패널은 스크립트 클로너(`BANG 클로너` Pseudo Effect + 표현식 레이어 복제, 래스터라이즈 지원)로 동작합니다.
- **Gradient (네이티브 이펙트 `BANG Gradient`)** — 정지점 최대 8개(색·위치·불투명도)짜리 그라데이션. 모양은 `Linear`·`Radial`·`Angular`·`Diamond`·`Reflected`·**`Contour`**(알파 경계까지의 거리 — 글자·도형 윤곽을 따라 흐릅니다), 보간은 `sRGB`·`Linear`·**`OKLab`**·`OKLCh`(색상환), `Repeat/Mirror`·`Cycles`·`Phase`·`Reverse`, 띠 없애는 `Dither`, 원본과의 블렌드·`Preserve Alpha`. `Fit Horizontal`·`Fit Vertical`·`Fit Diagonal` 버튼이 레이어 내용 크기에 맞춰 Start·End 를 잡아 주고(누를 때마다 좌↔우 / 상↔하 / 사분면 시계방향으로 뒤집힙니다), `Lock Gradient` 를 켜면 크기·위치가 바뀌어도 계속 따라갑니다. 프리셋·`Randomize`(OKLCh 감마 경계까지 써서 탁한 색을 피합니다)·`.css`/`.ggr`/`.json` 임포트·익스포트, 더블클릭으로 정지점 추가되는 Stops Bar. 정지점은 `Stop N` 접힐 그룹. 8/16/32bpc.
- **Stroke (네이티브 이펙트 `BANG Stroke`)** — 레이어 스타일 Stroke 의 대체. 알파 경계까지의 거리로 획을 그립니다. **획 하나 = 이펙트 하나**, 이펙트를 두 번 이상 적용하면 **앞 획 바깥에** 쌓여 동심 러리가 됩니다(`Position` Outside/Center/Inside · `Corner` **Round/Miter/Bevel** + `Miter Limit` · `Width` · `Offset` · `Softness` · `Opacity` · `Blend`), 색은 **단색 또는 그라데이션**(Across Stroke / Linear / Radial · Angle · Scale · **Opacity A/B** · Reverse), **Edge Noise**(Amount·Scale·Detail·Evolution·Seed)로 거친 가장자리, `Body`(Keep/Hide)·`Body Opacity`·`Order`. **`Fill Gaps`** 를 켜면 획이 거의 메운 글자 속 구멍을 획 색으로 마저 채웁니다(`Narrow Gaps` = `Gap Size` 보다 좁은 구멍만, `All Counters` = 카운터 전부). 레이어 경계 밖의 획도 잘리지 않습니다. 8/16/32bpc.
- **Align 3D** — 3D 레이어가 켜져 있어도 동작하는 정렬/분배(가로·세로 정렬 6종 + 양축 중앙 + 가로/세로 분배). 기준은 `Selection ⇅ Comp` 스위치.
- **Bento Grid** — 선택한 2D 푸티지/프리컴프/셰이프/텍스트 레이어를 모듈 그리드(1×1~4×4 타일)에 패킹. Tools 타일을 누르면 동봉한 `BentoGrid.jsx` 팔레트 창(Unit/Gap/Width·Fit·Variety·Packing Style·Crop/Center/Mix, Repack·Randomize·Clear Masks)이 열립니다.
- **Quote Align (따옴표 정렬)** — 인용 자막의 여는 따옴표를 본문 정렬선 밖으로 내어쓰기(행잉 펑추에이션).
  박스 텍스트는 세트 적용, 포인트 텍스트는 박스 텍스트로 자동 변환(원본은 대체되며 Ctrl+Z로 복원). *AE 2024(24.3) 이상 필요.*

## 🖥 요구 사항

- Windows
- After Effects CC 2022 (22.0) 이상
  — 단, **Quote Align** 기능은 After Effects 2024 (24.3) 이상에서만 동작

## 📦 설치 (미서명 CEP 확장)

1. `EnablePlayerDebugMode.reg` 실행, 또는 레지스트리에서
   `HKEY_CURRENT_USER\SOFTWARE\Adobe\CSXS.11` 에 문자열 값 `PlayerDebugMode = 1`
   (사용 중인 AE 버전에 맞는 `CSXS.N`).
2. `com.bang.toolbox` 폴더를
   `C:\Program Files (x86)\Common Files\Adobe\CEP\extensions\` 에 복사.
3. After Effects → `창(Window) > 확장(Extensions) > BANG_Toolbox`.

> 배포본(zip)에는 `INSTALL.txt` 와 `EnablePlayerDebugMode.reg` 가 함께 들어 있습니다.

## 🛠 기술

- Adobe CEP (CSXS 9+) · ExtendScript (After Effects scripting API)
- 순수 HTML · CSS · JavaScript 패널 — 외부 런타임/네트워크 의존 없이 오프라인 동작
- rem 기반 반응형 UI — 패널 폭·디스플레이 해상도에 따라 비율 유지

## 👤 Credits

- 제작 · 디자인: **방명환**

---

아이콘: [Phosphor Icons](https://phosphoricons.com) (MIT)
