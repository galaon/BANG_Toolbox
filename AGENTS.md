# AGENTS.md — BANG_Toolbox 코드 원칙

AI 코딩 에이전트와 사람 기여자 공통. 짧게, 그리고 지켜지는 것만 적는다.

## 1. 사다리 (코드를 쓰기 전에, 처음 통과하는 단계에서 멈춘다)

1. **있어야 하나?** 추측성 필요 → 만들지 않고 한 줄로 이유를 남긴다.
2. **이미 이 코드베이스에 있나?** `hostscript.jsx` 의 `_layerBoundsAtTime`, `_maskBoundsAtTime`, probe 표현식 평가(toComp/toWorld/fromWorldVec) 패턴, `ok()/err()`, `qh_try` — 재구현이 가장 흔한 낭비.
3. **AE 네이티브/표준으로 되나?** 표현식·Pseudo Effect·`applyPreset`·`sourceRectAtTime`·`hangingRoman` 처럼 AE 가 이미 제공하면 그것을 쓴다. 패널 JS 에는 외부 라이브러리 없음(오프라인 동작).
4. **한 줄로 되나?** 한 줄.
5. **그제서야** 돌아가는 최소한.

사다리는 문제를 **이해한 뒤**에 탄다. 수정이 닿는 함수의 호출처를 전부 grep 하고, 실제 흐름을 따라간 뒤 단계를 고른다. 버그는 증상이 아니라 근원(공용 함수 한 곳)에서 고친다.

## 2. 절대 줄이지 않는 것

입력 검증(빈 선택·컴프 없음·타입 불일치 → `err("한국어 이유")`), 단일 undo 그룹(`beginUndoGroup`/`endUndoGroup` — 예외 경로 포함), 임시 객체(probe null·임시 컴프) 정리, 접근성(`aria-label`, `aria-hidden`, `:focus-visible`), 사용자가 명시한 요구.

## 3. 검증 없는 코드는 미완성

- 비-trivial 로직(분기·루프·좌표 변환·파서)은 **실행 가능한 체크 1개**를 남긴다: `tools/test_scene_*.jsx`(AE 씬) 또는 `node -e`(예: `js/treemap.js` 는 Node 에서 면적 검증). 프레임워크·픽스처 없음.
- "됐다"는 AE 2026 에서 `tools/cep-eval.js` 로 실측한 뒤에만 말한다. 좌표·크기·개수처럼 **숫자로 기대값을 먼저 적고** 비교한다.

## 4. 코드 규약

- ExtendScript = ES3: `var` 만, `let/const/=>/템플릿/forEach/JSON 이외 ES5+` 금지, **중첩 삼항 금지**(파서 버그). 파일은 UTF‑8 **BOM**, 한글 문자열 허용.
- jsx 진입 함수는 JSON 문자열 반환 `ok({...})` / `err(msg)`; 패널은 `JSON.parse` → `setStatus(msg, 'success'|'error')`. 오류 문구는 원인이 드러나는 한국어.
- 표현식 텍스트는 파라미터를 **이름**으로 참조(`effect("BANG 클로너")("복제 개수")`), 클론/자식은 Layer Control 로 소스를 가리킨다(인덱스·이름 변경에 안전).
- CSS 는 `:root` 토큰만(`--bg-*`, `--text-*`, `--accent*`, `--radius-*`, `--tr-*`), 정적 배경에 `rgba` 금지, 치수는 rem. 새 위젯은 기존 카드(`.card`) 안에 접두어(`qa-`, `pf-`, `cl-`, `tm-`, `al-`)로.
- 아이콘: 라벨 있는 버튼의 SVG 는 `aria-hidden`, 아이콘 전용 버튼은 `aria-label`. 새 아이콘은 Phosphor regular 또는 사용자 제공 SVG(`currentColor`).
- 삭제가 추가보다 낫다. 파일 수 최소. 요청되지 않은 추상화(구현 하나뿐인 인터페이스, 값이 바뀌지 않는 설정) 금지.

## 5. 출력 형식 (에이전트)

코드 먼저. 그다음 최대 세 줄: 무엇을 건너뛰었고, 언제 추가할지. 설명이 코드보다 길면 설명을 지운다. 사용자가 보고서를 요청한 경우는 예외.

---
원칙의 골자는 [ponytail](https://github.com/DietrichGebert/ponytail)(MIT) 의 사다리를 이 프로젝트에 맞게 줄인 것이다.
