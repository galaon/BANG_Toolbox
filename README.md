# BANG_Toolbox

>create Null · Null 생성
>move Anchor Point · 앵커포인트 이동
>Color Picker · 색상선택



## ✨ 기능

- **Green Null**
- — 선택한 레이어에 Null을 만들고 자동으로 Parents 연결.
  — 마스크가 있는 레이어는 이미지 원본 중앙이 아니라 **마스크 영역의 중심**에 Null을 생성합니다.
  — 여러개의 오브젝트는 **각 중심의 평균점**에 Null을 생성합니다.
- **Anchor Point (9방향)** — 3×3 버튼으로 앵커포인트를 이동. 시각적 위치는 그대로 유지되도록
  Scale·Z회전을 반영해 Position을 자동 보정하며, 마스크가 있으면 **마스크 영역 기준**으로 정렬합니다.
- **Color Picker** — AE 네이티브 컬러 피커를 호출해 색을 추출하고 HEX / RGB / HSB 표시 + 색상 히스토리 저장.

## 🖥 요구 사항

- Windows
- After Effects CC 2022 (22.0) 이상

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

- 제작 · 디자인: **방명환** (방송 그래픽 디자이너)

---

아이콘: [Phosphor Icons](https://phosphoricons.com) (MIT)
