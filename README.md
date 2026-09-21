# BANG_Toolbox

>create Null · Null 생성<br>
>move Anchor Point · 앵커포인트 이동<br>
>Align 3D · 3D 정렬<br>
>Precomp Fit · 프리컴프 크기 맞춤<br>
>Cloner · 복제<br>
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
- **Align 3D** — 3D 레이어가 켜져 있어도 동작하는 정렬/분배. 좌·중·우 / 상·중·하 / (Z 토글) 앞·중·뒤, 분배 X·Y·Z.
  기준은 선택 영역(기본) 또는 컴프. 부모·회전·스케일이 있어도 월드 공간 경계 기준으로 정확히 이동합니다.
- **Precomp Fit** — 프리컴프 크기를 내부 레이어 경계(+여백)에 맞게 줄이거나 늘립니다. 부모 컴프에서 프리컴프 레이어를 선택해
  실행하거나 프리컴프 안에서 실행. 모든 인스턴스의 앵커를 보정해 **시각적 위치가 변하지 않습니다**. 현재 프레임(기본) / 전체 프레임(All) 옵션.
- **Cloner** — Cinema 4D Cloner 를 참고한 라이브 복제. 소스 레이어에 `Cloner Mode / Count / Offset / Grid / Radius / Step / Random` 컨트롤을
  삽입하고, 클론은 표현식으로 소스 컨트롤을 따라 실시간 재배치(Linear · Grid · Radial). 소스를 옮기면 전체가 따라옵니다.
  Count 변경 후 버튼을 다시 누르면 재복제, × 버튼으로 클론 제거.
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
