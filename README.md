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
- **Color Picker** — AE 네이티브 컬러 피커를 호출해 색을 추출하고 HEX / RGB / HSB 표시 + 색상 히스토리 저장.
- **Precomp Fit** — 프리컴프 크기를 내부 레이어 경계(+여백)에 맞게 줄이거나 늘립니다. 부모 컴프에서 프리컴프 레이어를 선택해
  실행하거나 프리컴프 안에서 실행. 모든 인스턴스의 앵커를 보정해 **시각적 위치가 변하지 않습니다**. 현재 프레임(기본) / 전체 프레임(All) 옵션.
- **Cloner (네이티브 이펙트 `BANG Cloner`)** — Cinema 4D Cloner 를 참고한 인스턴스 클로너. 레이어를 복제하지 않고 소스의 현재 프레임을 N개 변환·합성하므로 **개수와 무관하게 애니메이션 타이밍이 정확**하고 가볍습니다(1000개 ≈ 0.1 s). 배치 선형/그리드/방사형 · 복제 개수·열/행·간격·반지름·각도 · 회전/크기 단계 · 끝 불투명도 · 랜덤(위치·회전·크기·시드). 배치에 맞지 않는 항목은 자동 숨김. `plugins/BANG_Cloner.aex` 설치 필요(INSTALL.txt 2‑1) — 미설치 시 패널은 스크립트 클로너(`BANG 클로너` Pseudo Effect + 표현식 레이어 복제, 래스터라이즈 지원)로 동작합니다.
- **Stroke (네이티브 이펙트 `BANG Stroke`)** — 레이어 스타일 Stroke 의 대체. 알파 경계까지의 거리로 획을 그려 바깥/중앙/안쪽 · 두께 · 오프셋(가장자리에서 띄우기) · 부드러움 · 본체 숨김(획만) · 합성 순서를 지원, 레이어 경계 밖의 획도 잘리지 않습니다. 8/16/32bpc.
- **Align 3D** — 3D 레이어가 켜져 있어도 동작하는 정렬/분배(가로·세로 정렬 6종 + 양축 중앙 + 가로/세로 분배). 기준은 `Selection ⇅ Comp` 스위치.
- **Bento Grid** — 선택한 2D 푸티지/프리컴프 레이어를 모듈 그리드(1×1~4×4 타일)에 패킹. Unit/Gap/Width·Fit(Cover/Contain)·Variety·Packing Style·Crop/Center/Mix, Repack·Randomize·Clear Masks. (`BentoGrid.jsx` v1.3.0 이식)
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
