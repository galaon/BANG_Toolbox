/* ============================================================
   main.js — UI 이벤트 & AE 호출 로직
   ============================================================ */

'use strict';

const csInterface = new CSInterface();

// JSX는 manifest.xml 의 <ScriptPath> 로 AE 시작 시 자동 로드됨.

// ── 유틸리티 ──────────────────────────────────────────────────

/**
 * 상태 바 메시지 업데이트
 * @param {string} msg
 * @param {'default'|'success'|'error'} type
 */
function setStatus(msg, type = 'default') {
  const el = document.getElementById('status-text');
  el.textContent = msg;
  el.className = 'statusbar__text';
  if (type === 'success') el.classList.add('statusbar__text--success');
  if (type === 'error')   el.classList.add('statusbar__text--error');
}

/**
 * ExtendScript 함수 호출 래퍼
 * @param {string} fnCall   - 예: "createGreenNull()"
 * @param {function} callback - (result) => void
 */
function evalScript(fnCall, callback) {
  csInterface.evalScript(fnCall, (result) => {
    if (result === 'EvalScript error.') {
      setStatus('Error: ' + fnCall, 'error');
      console.error('evalScript failed:', fnCall, result);
      return;
    }
    if (callback) callback(result);
  });
}

// ── Anchor Point ─────────────────────────────────────────────

document.querySelectorAll('.ap-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const h = btn.dataset.h;
    const v = btn.dataset.v;
    setStatus('Setting anchor point...');
    evalScript(`setAnchorPoint(${h}, ${v})`, (result) => {
      try {
        const res = JSON.parse(result);
        if (res.success) {
          const n = res.count;
          setStatus(`Anchor point set (${n} layer${n !== 1 ? 's' : ''})`, 'success');
        } else {
          setStatus('Error: ' + res.error, 'error');
        }
      } catch (e) {
        setStatus('Unexpected response', 'error');
      }
    });
  });
});

// ── Color Picker ─────────────────────────────────────────────
//
//  자체 피커다. 예전에는 보이지 않는 Null + Color Control 이펙트를 만들고
//  executeCommand(2240) 으로 AE 네이티브 다이얼로그를 띄워 색을 받아왔는데,
//  그 방식은 컴프를 건드리고 undo 를 더럽히며 AE 가 떠 있어야만 동작했다. 이제는
//    · 색 고르기 = 패널 안 HSV 사각형 + 색상/불투명도 슬라이더 + HEX/RGB/HSB/OKLCH 입력
//    · 화면에서 집기 = bin/BANG_Picker.exe (확대 루페가 달린 전체화면 오버레이)
//  두 가지로 나뉜다.
//
//  ⚠ CEP(Chromium 99)에서 화면 픽셀을 읽는 브라우저 경로는 전부 막혀 있다 — 실측:
//     · window.EyeDropper 는 존재하지만 open() 이 2ms 만에 AbortError (CEF 가 오버레이를 못 띄움)
//     · navigator.mediaDevices.getDisplayMedia 는 NotAllowedError: Permission denied
//     그래서 화면 집기는 네이티브 도우미가 맡는다. 다시 조사하지 말 것.

const CP_HISTORY_KEY = 'bang-toolbox-cp-history';
const CP_HISTORY_MAX = 12;

// 상태는 HSV + alpha 로 들고 있는다. hex 만 들고 있으면 채도 0(흰·검)에서 색상을 잃어버려
// 색상 슬라이더가 제멋대로 튄다 — 피커에서 가장 흔한 버그다.
let cpH = 123, cpS = 0.566, cpV = 0.686, cpA = 1;
let cpCurrentHex = '#4CAF50';
let cpModel = 'rgb';          // 숫자 세 칸이 무엇을 보여줄지 (rgb | hsb | oklch)
let cpEditing = false;        // 입력 중에는 그 칸을 덮어쓰지 않는다

// 리브랜딩: 구 키(aegreatagain-cp-history)에 저장된 히스토리를 신규 키로 1회 이관.
(function cpMigrateLegacyHistory() {
  try {
    const LEGACY = 'aegreatagain-cp-history';
    if (localStorage.getItem(CP_HISTORY_KEY) === null) {
      const old = localStorage.getItem(LEGACY);
      if (old !== null) localStorage.setItem(CP_HISTORY_KEY, old);
    }
    localStorage.removeItem(LEGACY);
  } catch (e) { /* localStorage 불가 환경 무시 */ }
})();

// ── 색 변환 ──────────────────────────────────────────────────

const cpClamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

function cpHexToRgb(hex) {
  const h = hex.replace('#', '');
  return { r: parseInt(h.slice(0, 2), 16), g: parseInt(h.slice(2, 4), 16), b: parseInt(h.slice(4, 6), 16) };
}

function cpRgbToHex(r, g, b) {
  const t = (v) => ('0' + Math.round(cpClamp(v, 0, 255)).toString(16)).slice(-2);
  return ('#' + t(r) + t(g) + t(b)).toUpperCase();
}

// HSV(0~360, 0~1, 0~1) → RGB(0~255)
function cpHsvToRgb(h, s, v) {
  h = ((h % 360) + 360) % 360;
  const c = v * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = v - c;
  let r = 0, g = 0, b = 0;
  if      (h <  60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else              { r = c; b = x; }
  return { r: (r + m) * 255, g: (g + m) * 255, b: (b + m) * 255 };
}

// RGB(0~255) → HSV. 무채색이면 색상을 유지한다(fallbackH)
function cpRgbToHsv(r, g, b, fallbackH) {
  const rn = r / 255, gn = g / 255, bn = b / 255;
  const max = Math.max(rn, gn, bn), min = Math.min(rn, gn, bn), d = max - min;
  let h = fallbackH || 0;
  if (d > 1e-9) {
    if      (max === rn) h = ((gn - bn) / d) % 6;
    else if (max === gn) h = (bn - rn) / d + 2;
    else                 h = (rn - gn) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
  }
  return { h: h, s: max === 0 ? 0 : d / max, v: max };
}

// sRGB ↔ 선형
const cpSrgbToLin = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const cpLinToSrgb = (c) => (c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055);

// OKLab (Björn Ottosson). BANG Gradient 의 C++ 쪽과 같은 계수를 쓴다.
function cpRgbToOklch(r, g, b) {
  const lr = cpSrgbToLin(r / 255), lg = cpSrgbToLin(g / 255), lb = cpSrgbToLin(b / 255);
  const l = Math.cbrt(0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb);
  const m = Math.cbrt(0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb);
  const s = Math.cbrt(0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb);
  const L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
  const A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
  const B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
  let H = Math.atan2(B, A) * 180 / Math.PI;
  if (H < 0) H += 360;
  return { L: L, C: Math.sqrt(A * A + B * B), H: H };
}

function cpOklchToRgbRaw(L, C, H) {
  const a = C * Math.cos(H * Math.PI / 180), b = C * Math.sin(H * Math.PI / 180);
  const l_ = L + 0.3963377774 * a + 0.2158037573 * b;
  const m_ = L - 0.1055613458 * a - 0.0638541728 * b;
  const s_ = L - 0.0894841775 * a - 1.2914855480 * b;
  const l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  return {
    r: cpLinToSrgb( 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s) * 255,
    g: cpLinToSrgb(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s) * 255,
    b: cpLinToSrgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s) * 255
  };
}

// OKLCH → sRGB. 감마 밖이면 **채널을 자르지 않고 채도를 줄여** 맞춘다.
// 그냥 자르면 색상이 틀어지고 탁해진다 (BANG Gradient 의 Randomize 와 같은 원칙).
function cpOklchToRgb(L, C, H) {
  const inGamut = (c) => {
    const p = cpOklchToRgbRaw(L, c, H);
    return p.r >= -0.5 && p.r <= 255.5 && p.g >= -0.5 && p.g <= 255.5 && p.b >= -0.5 && p.b <= 255.5;
  };
  let c = C;
  if (!inGamut(c)) {
    let lo = 0, hi = C;
    for (let i = 0; i < 20; i++) { const mid = (lo + hi) / 2; if (inGamut(mid)) lo = mid; else hi = mid; }
    c = lo;
  }
  const p = cpOklchToRgbRaw(L, c, H);
  return { r: cpClamp(p.r, 0, 255), g: cpClamp(p.g, 0, 255), b: cpClamp(p.b, 0, 255) };
}

// hex 정규화 (#RRGGBB. 3·4·8자리도 받는다 — 8자리면 알파까지)
function cpNormHex(raw) {
  let h = String(raw).replace(/[^0-9a-fA-F]/g, '');
  if (h.length === 3 || h.length === 4) h = h.split('').map(c => c + c).join('');
  if (h.length === 8) h = h.slice(0, 6);
  if (h.length !== 6) return null;
  return '#' + h.toUpperCase();
}

// ── 현재 색 ──────────────────────────────────────────────────

function cpRgb() { return cpHsvToRgb(cpH, cpS, cpV); }

function cpFormat(kind) {
  const { r, g, b } = cpRgb();
  const R = Math.round(r), G = Math.round(g), B = Math.round(b);
  if (kind === 'rgb')   return cpA < 1 ? `rgba(${R}, ${G}, ${B}, ${+cpA.toFixed(3)})` : `rgb(${R}, ${G}, ${B})`;
  if (kind === 'oklch') {
    const o = cpRgbToOklch(R, G, B);
    const base = `${(o.L * 100).toFixed(1)}% ${o.C.toFixed(3)} ${o.H.toFixed(1)}`;
    return cpA < 1 ? `oklch(${base} / ${+cpA.toFixed(3)})` : `oklch(${base})`;
  }
  if (kind === 'ae')    return `[${(r / 255).toFixed(4)}, ${(g / 255).toFixed(4)}, ${(b / 255).toFixed(4)}, 1]`;
  return cpCurrentHex;
}

// ── UI 전체 갱신 ──────────────────────────────────────────────

function cpUpdateUI() {
  const { r, g, b } = cpRgb();
  const R = Math.round(r), G = Math.round(g), B = Math.round(b);
  cpCurrentHex = cpRgbToHex(R, G, B);

  const prev = document.getElementById('cp-preview');
  if (prev) prev.style.background = cpCurrentHex;
  const hexIn = document.getElementById('cp-hex-val');
  if (hexIn && document.activeElement !== hexIn) hexIn.value = cpCurrentHex.slice(1);

  // 사각형은 현재 색상(hue)의 순색을 바닥에 깐다
  const sv = document.getElementById('cp-sv');
  if (sv) {
    const pure = cpHsvToRgb(cpH, 1, 1);
    sv.style.background = cpRgbToHex(pure.r, pure.g, pure.b);
    const k = document.getElementById('cp-sv-knob');
    k.style.left = (cpS * 100) + '%';
    k.style.top  = ((1 - cpV) * 100) + '%';
  }
  const hk = document.getElementById('cp-hue-knob');
  if (hk) hk.style.left = ((cpH / 360) * 100) + '%';
  const af = document.getElementById('cp-alpha-fill');
  if (af) af.style.background = `linear-gradient(to right, rgba(${R},${G},${B},0), rgb(${R},${G},${B}))`;
  const ak = document.getElementById('cp-alpha-knob');
  if (ak) ak.style.left = (cpA * 100) + '%';

  if (!cpEditing) cpRenderFields();
}

function cpRenderFields() {
  const { r, g, b } = cpRgb();
  let keys, vals;
  if (cpModel === 'hsb') {
    keys = ['H', 'S', 'B'];
    vals = [Math.round(cpH), Math.round(cpS * 100), Math.round(cpV * 100)];
  } else if (cpModel === 'oklch') {
    const o = cpRgbToOklch(Math.round(r), Math.round(g), Math.round(b));
    keys = ['L', 'C', 'H'];
    vals = [(o.L * 100).toFixed(1), o.C.toFixed(3), Math.round(o.H)];
  } else {
    keys = ['R', 'G', 'B'];
    vals = [Math.round(r), Math.round(g), Math.round(b)];
  }
  for (let i = 0; i < 3; i++) {
    document.getElementById('cp-k' + i).textContent = keys[i];
    const el = document.getElementById('cp-n' + i);
    if (document.activeElement !== el) el.value = vals[i];
  }
}

// HSV 를 직접 세팅
function cpSetHsv(h, s, v, a) {
  cpH = ((h % 360) + 360) % 360;
  cpS = cpClamp(s, 0, 1);
  cpV = cpClamp(v, 0, 1);
  if (a !== undefined) cpA = cpClamp(a, 0, 1);
  cpUpdateUI();
}

// hex 로 세팅 (무채색이어도 색상은 유지)
function cpSetHex(hex) {
  const n = cpNormHex(hex);
  if (!n) return false;
  const { r, g, b } = cpHexToRgb(n);
  const hsv = cpRgbToHsv(r, g, b, cpH);
  cpSetHsv(hsv.h, hsv.s, hsv.v);
  return true;
}

// ── 히스토리 ─────────────────────────────────────────────────

function cpLoadHistory() {
  try { return JSON.parse(localStorage.getItem(CP_HISTORY_KEY) || '[]'); }
  catch (e) { return []; }
}

function cpAddToHistory(hex) {
  let h = cpLoadHistory();
  h = [hex, ...h.filter(c => c !== hex)].slice(0, CP_HISTORY_MAX);
  localStorage.setItem(CP_HISTORY_KEY, JSON.stringify(h));
  cpRenderHistory();
}

function cpRenderHistory() {
  const container = document.getElementById('cp-history');
  const history   = cpLoadHistory();
  container.innerHTML = '';

  history.forEach(hex => {
    const btn = document.createElement('button');
    btn.className        = 'cp-swatch';
    btn.style.background = hex;
    btn.title            = hex.toUpperCase() + ' — 클릭하면 이 색으로, 복사까지';
    btn.setAttribute('aria-label', btn.title);
    btn.addEventListener('click', () => {
      cpSetHex(hex);
      cpCopyCurrent();
    });
    container.appendChild(btn);
  });

  for (let i = history.length; i < CP_HISTORY_MAX; i++) {
    const slot = document.createElement('div');
    slot.className = 'cp-swatch cp-swatch--empty';
    container.appendChild(slot);
  }
}

// ── 클립보드 ─────────────────────────────────────────────────

let cpToastTimer = null;
function cpToast(text) {
  const el = document.getElementById('cp-toast');
  if (!el) return;
  el.textContent = text || '복사 완료!';
  el.hidden = false;
  requestAnimationFrame(() => el.classList.add('is-on'));
  clearTimeout(cpToastTimer);
  cpToastTimer = setTimeout(() => { el.classList.remove('is-on'); setTimeout(() => { el.hidden = true; }, 180); }, 1200);
}

function cpCopyText(text) {
  const fallback = () => {
    try {
      const ta = document.createElement('textarea');
      ta.value = text;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand('copy');
      document.body.removeChild(ta);
      setStatus('Copied: ' + text, 'success'); cpToast('복사 완료!');
    } catch (e) { setStatus('Copy failed', 'error'); }
  };
  if (navigator.clipboard) {
    navigator.clipboard.writeText(text)
      .then(() => { setStatus('Copied: ' + text, 'success'); cpToast('복사 완료!'); })
      .catch(fallback);
  } else fallback();
}

function cpCopyCurrent() {
  const fmt = document.getElementById('cp-fmt');
  cpCopyText(cpFormat(fmt ? fmt.value : 'hex'));
}

// ── 사각형 · 슬라이더 드래그 ──────────────────────────────────

// 포인터를 캡처해서 요소 밖으로 끌어도 계속 따라오게 한다 (포토샵·피그마와 같은 감각)
function cpDrag(el, onMove, onEnd) {
  const handle = (e) => {
    const r = el.getBoundingClientRect();
    onMove(cpClamp((e.clientX - r.left) / r.width, 0, 1),
           cpClamp((e.clientY - r.top) / r.height, 0, 1));
  };
  el.addEventListener('pointerdown', (e) => {
    el.setPointerCapture(e.pointerId);
    el.focus();
    handle(e);
    e.preventDefault();
  });
  el.addEventListener('pointermove', (e) => { if (el.hasPointerCapture(e.pointerId)) handle(e); });
  el.addEventListener('pointerup', (e) => {
    if (el.hasPointerCapture(e.pointerId)) el.releasePointerCapture(e.pointerId);
    if (onEnd) onEnd();
  });
}

// 방향키로 1칸(Shift=10칸) 미세조정
function cpArrows(el, stepX, stepY, onEnd) {
  el.addEventListener('keydown', (e) => {
    const m = e.shiftKey ? 10 : 1;
    let dx = 0, dy = 0;
    if      (e.key === 'ArrowLeft')  dx = -m;
    else if (e.key === 'ArrowRight') dx =  m;
    else if (e.key === 'ArrowUp')    dy = -m;
    else if (e.key === 'ArrowDown')  dy =  m;
    else return;
    e.preventDefault();
    stepX(dx); if (stepY) stepY(dy);
    cpUpdateUI();
    if (onEnd) onEnd();
  });
}

// ── 네이티브 스포이드 (bin/BANG_Picker.exe) ────────────────────

function cpTempDir() {
  let base = '';
  try {
    base = csInterface.getSystemPath(SystemPath.USER_DATA) || '';
    if (/^file:/i.test(base)) base = decodeURIComponent(base.replace(/^file:\/{2,3}/i, ''));
  } catch (e) { return ''; }
  const dir = (base + '/BANG_Toolbox').replace(/\\/g, '/');
  try { window.cep.fs.makedir(dir); } catch (e) { /* 이미 있으면 그만 */ }
  return dir;
}

const eyedropperBtn = document.getElementById('cp-eyedropper-btn');
const cpStatusEl    = document.getElementById('cp-status');
let   cpPickActive  = false;

function cpResetPickState() {
  cpPickActive = false;
  eyedropperBtn.classList.remove('cp-pick-btn--active');
  cpStatusEl.textContent = '';
}

eyedropperBtn.addEventListener('click', () => {
  if (cpPickActive) return;

  const exe = extPath('bin/BANG_Picker.exe');
  const dir = cpTempDir();
  if (!dir) { setStatus('임시 폴더를 만들 수 없습니다', 'error'); return; }
  const out = dir + '/pick_' + Date.now() + '.txt';

  let proc;
  try { proc = window.cep.process.createProcess(exe, out); } catch (e) { proc = null; }
  if (!proc || proc.err !== 0 || proc.data < 0) {
    setStatus('스포이드 도우미를 실행할 수 없습니다 — bin/BANG_Picker.exe 확인', 'error');
    cpStatusEl.textContent = 'BANG_Picker.exe not found';
    return;
  }

  cpPickActive = true;
  eyedropperBtn.classList.add('cp-pick-btn--active');
  cpStatusEl.textContent = '화면에서 색을 고르세요 — 휠=확대, 방향키=1px, Esc=취소';
  setStatus('화면에서 색을 고르세요 (Esc 취소)');

  window.cep.process.onquit(proc.data, () => {
    cpResetPickState();
    let res = null;
    try { res = window.cep.fs.readFile(out); } catch (e) { res = null; }
    try { window.cep.fs.deleteFile(out); } catch (e) { /* 남아도 무해 */ }

    const hex = res && res.err === 0 ? cpNormHex(res.data) : null;
    if (!hex) { setStatus('Cancelled', 'default'); return; }
    cpSetHex(hex);
    cpAddToHistory(cpCurrentHex);
    cpCopyCurrent();      // 집자마자 클립보드로 — 바로 붙여넣을 수 있게
  });
});

// ── AE 와 주고받기 ────────────────────────────────────────────

document.getElementById('cp-apply-btn').addEventListener('click', () => {
  const { r, g, b } = cpRgb();
  setStatus('색 적용 중...');
  evalScript(`applyColorToSelection(${(r / 255).toFixed(6)}, ${(g / 255).toFixed(6)}, ${(b / 255).toFixed(6)})`, (raw) => {
    let res; try { res = JSON.parse(raw); } catch (e) { res = { success: false, error: String(raw) }; }
    if (res.success) setStatus(res.message || '적용했습니다', 'success');
    else setStatus(res.error || '적용 실패', 'error');
  });
});

document.getElementById('cp-read-btn').addEventListener('click', () => {
  setStatus('선택에서 색 읽는 중...');
  evalScript('readColorFromSelection()', (raw) => {
    let res; try { res = JSON.parse(raw); } catch (e) { res = { success: false, error: String(raw) }; }
    if (!res.success) { setStatus(res.error || '색을 찾지 못했습니다', 'error'); return; }
    const hex = cpNormHex(res.hex);
    if (!hex) { setStatus('색을 찾지 못했습니다', 'error'); return; }
    cpSetHex(hex);
    cpAddToHistory(cpCurrentHex);
    setStatus((res.message || 'Read') + ': ' + cpCurrentHex, 'success');
  });
});

document.getElementById('cp-copy-btn').addEventListener('click', cpCopyCurrent);

document.getElementById('cp-clear-btn').addEventListener('click', () => {
  localStorage.removeItem(CP_HISTORY_KEY);
  cpRenderHistory();
  setStatus('History cleared');
});

// ── 입력 배선 ────────────────────────────────────────────────

(function cpWire() {
  const commit = () => cpAddToHistory(cpCurrentHex);

  const sv = document.getElementById('cp-sv');
  cpDrag(sv, (x, y) => cpSetHsv(cpH, x, 1 - y), commit);
  cpArrows(sv, (d) => { cpS = cpClamp(cpS + d / 100, 0, 1); }, (d) => { cpV = cpClamp(cpV - d / 100, 0, 1); }, commit);

  const hue = document.getElementById('cp-hue-bar');
  cpDrag(hue, (x) => cpSetHsv(x * 360, cpS, cpV), commit);
  cpArrows(hue, (d) => { cpH = ((cpH + d) % 360 + 360) % 360; }, null, commit);

  const alpha = document.getElementById('cp-alpha-bar');
  cpDrag(alpha, (x) => cpSetHsv(cpH, cpS, cpV, x), commit);
  cpArrows(alpha, (d) => { cpA = cpClamp(cpA + d / 100, 0, 1); }, null, commit);

  // hex 입력
  const hexIn = document.getElementById('cp-hex-val');
  hexIn.addEventListener('focus', () => hexIn.select());
  hexIn.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { hexIn.blur(); }
    else if (e.key === 'Escape') { hexIn.value = cpCurrentHex.slice(1); hexIn.blur(); }
  });
  hexIn.addEventListener('blur', () => {
    if (cpSetHex(hexIn.value)) cpAddToHistory(cpCurrentHex);
    else hexIn.value = cpCurrentHex.slice(1);
  });

  // 모델 전환
  document.querySelectorAll('.cp-model').forEach(btn => {
    btn.addEventListener('click', () => {
      cpModel = btn.dataset.model;
      document.querySelectorAll('.cp-model').forEach(b => b.classList.toggle('is-on', b === btn));
      cpRenderFields();
    });
  });

  // 숫자 세 칸 — 타이핑 + ↑↓
  for (let i = 0; i < 3; i++) {
    const el = document.getElementById('cp-n' + i);
    el.addEventListener('focus', () => { cpEditing = true; el.select(); });
    el.addEventListener('blur',  () => { cpEditing = false; cpApplyFields(); cpRenderFields(); });
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { el.blur(); return; }
      if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
      e.preventDefault();
      const stepBase = (cpModel === 'oklch' && i === 1) ? 0.005 : 1;
      const step = (e.shiftKey ? 10 : 1) * stepBase * (e.key === 'ArrowUp' ? 1 : -1);
      el.value = (parseFloat(el.value || '0') + step).toFixed(stepBase < 1 ? 3 : (cpModel === 'oklch' && i === 0 ? 1 : 0));
      cpApplyFields();
    });
  }
})();

// 숫자 칸 → 색. 모델에 따라 해석이 다르다.
function cpApplyFields() {
  const n = [0, 1, 2].map(i => parseFloat(document.getElementById('cp-n' + i).value));
  if (n.some(v => isNaN(v))) return;
  if (cpModel === 'hsb') {
    cpSetHsv(n[0], n[1] / 100, n[2] / 100);
  } else if (cpModel === 'oklch') {
    const p = cpOklchToRgb(cpClamp(n[0] / 100, 0, 1), Math.max(0, n[1]), n[2]);
    const hsv = cpRgbToHsv(p.r, p.g, p.b, n[2]);
    cpSetHsv(hsv.h, hsv.s, hsv.v);
  } else {
    const hsv = cpRgbToHsv(cpClamp(n[0], 0, 255), cpClamp(n[1], 0, 255), cpClamp(n[2], 0, 255), cpH);
    cpSetHsv(hsv.h, hsv.s, hsv.v);
  }
}

// ── 초기화 ───────────────────────────────────────────────────

cpSetHex('#4CAF50');
cpRenderHistory();

// ── Green Null Creator ────────────────────────────────────────

document.getElementById('btn-create-green-null').addEventListener('click', () => {
  setStatus('Creating Green Null...');
  evalScript('createGreenNull()', (result) => {
    try {
      const res = JSON.parse(result);
      if (res.success) {
        let msg = res.parented > 0
          ? `"${res.name}" created — ${res.parented} layer(s) parented`
          : `"${res.name}" created`;
        if (res.centered) msg += ' · centered on selection';
        setStatus(msg, 'success');
      } else {
        setStatus('Error: ' + res.error, 'error');
      }
    } catch (e) {
      setStatus('Unexpected response', 'error');
    }
  });
});

// ── Quote Align (따옴표 정렬) ─────────────────────────────────

document.getElementById('btn-quote-align').addEventListener('click', () => {
  setStatus('Aligning quotes...');
  evalScript('applyQuoteHang()', (result) => {
    try {
      const res = JSON.parse(result);
      if (res.success) {
        const parts = [];
        if (res.rebuilt) parts.push(`${res.rebuilt} converted`);
        if (res.boxSet)  parts.push(`${res.boxSet} box set`);
        if (res.skipped) parts.push(`${res.skipped} skipped`);
        if (res.errors)  parts.push(`${res.errors} error(s)`);
        setStatus('Quote Align: ' + (parts.join(' · ') || 'nothing to do'),
                  res.errors ? 'error' : 'success');
      } else {
        setStatus('Error: ' + res.error, 'error');
      }
    } catch (e) {
      setStatus('Unexpected response', 'error');
    }
  });
});

// ── Precomp Fit ───────────────────────────────────────────────


// 여백(Pad)은 프리컴프 안의 'BANG Crop' 컨트롤러(Slider "Pad (px)")가 정한다.
// 자동 재실행: 컨트롤러의 Pad 가 마지막으로 적용한 값과 달라진 채 1초 안정되면 Crop 을 다시 실행
(function initCropAuto() {
  let last = null, stable = 0, busy = false;
  setInterval(() => {
    if (busy) return;
    if (/^Crop Precomp\.\.\./.test(document.getElementById('status-text').textContent)) return;   // 실행 중
    busy = true;
    evalScript('cropPollState()', (result) => {
      busy = false;
      try {
        const st = JSON.parse(result);
        if (!st.success || !st.active || !st.dirty) { last = null; stable = 0; return; }
        const key = `${st.comp}|${st.pad}|${st.all ? 1 : 0}`;
        if (key === last) stable++; else { last = key; stable = 0; }
        if (stable >= 1) { stable = 0; document.getElementById('btn-precomp-fit').click(); }
      } catch (e) { /* ignore */ }
    });
  }, 700);
})();

document.getElementById('btn-precomp-fit').addEventListener('click', () => {
  setStatus('Crop Precomp...');
  evalScript('fitPrecomp()', (result) => {
    try {
      const res = JSON.parse(result);
      if (res.success) {
        const parts = res.fitted.map(f =>
          `${f.name}: ${f.from[0]}×${f.from[1]} → ${f.to[0]}×${f.to[1]}` +
          (f.pad ? ` pad ${f.pad}` : '') + (f.mode === 'all' ? ' (all frames)' : '') + (f.instances ? ` (${f.instances} inst.)` : ''));
        let msg = 'Crop Precomp — ' + parts.join(' · ') + " (여백은 프리컴프의 'BANG Crop' > Pad)";
        if (res.warnings.length) msg += ` · ${res.warnings.length} warning(s)`;
        setStatus(msg, res.warnings.length ? 'default' : 'success');
        if (res.warnings.length) console.warn('Crop Precomp warnings:', res.warnings);
      } else {
        setStatus('Error: ' + res.error, 'error');
      }
    } catch (e) {
      setStatus('Unexpected response', 'error');
    }
  });
});

// Align to 박스가 좁으면 "Align to" 라벨을 숨긴다 (CEP 의 CEF 가 container query 를 지원하지 않아 JS 로 측정)
(function initAlignSideFit() {
  const side = document.querySelector('.al-side');
  if (!side) return;
  const fit = () => {
    side.classList.remove('is-tight');
    if (side.scrollWidth > side.clientWidth + 1 || side.scrollHeight > side.clientHeight + 1) side.classList.add('is-tight');
  };
  if (window.ResizeObserver) new ResizeObserver(fit).observe(side);
  window.addEventListener('resize', fit);
  fit();
})();

// ── Align 3D ──────────────────────────────────────────────────

const alRefBtn = document.getElementById('al-ref');   // role=switch: false=Selection, true=Comp
alRefBtn.addEventListener('click', () => {
  const on = alRefBtn.getAttribute('aria-checked') !== 'true';
  alRefBtn.setAttribute('aria-checked', on ? 'true' : 'false');
});

document.querySelectorAll('.al-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const axis = btn.dataset.axis, mode = btn.dataset.mode;
    const ref  = alRefBtn.getAttribute('aria-checked') === 'true' ? 'comp' : 'selection';
    // "xy" = 가로·세로 중앙 동시 정렬 (x → y 순차 호출, 각각 undo 그룹)
    const calls = axis === 'xy' ? [['x', mode], ['y', mode]] : [[axis, mode]];
    setStatus(`Aligning ${axis.toUpperCase()} ${mode}...`);
    const run = (i, acc) => {
      if (i >= calls.length) {
        let msg = `Align ${axis.toUpperCase()} ${mode} (${ref}) — ${acc.moved} moved`;
        if (acc.skipped) msg += `, ${acc.skipped} skipped`;
        if (acc.warnings.length) { msg += `, ${acc.warnings.length} warning(s)`; console.warn(acc.warnings); }
        setStatus(msg, 'success');
        return;
      }
      evalScript(`alignLayers("${calls[i][0]}", "${calls[i][1]}", "${ref}")`, (result) => {
        try {
          const res = JSON.parse(result);
          if (!res.success) { setStatus('Error: ' + res.error, 'error'); return; }
          acc.moved += res.moved; acc.skipped += res.skipped; acc.warnings.push(...res.warnings);
          run(i + 1, acc);
        } catch (e) { setStatus('Unexpected response', 'error'); }
      });
    };
    run(0, { moved: 0, skipped: 0, warnings: [] });
  });
});

// ── Cloner ────────────────────────────────────────────────────
// 설정은 전부 소스 레이어의 단일 이펙트 "BANG 클로너"(jsx/BANG_Cloner.ffx). 패널 버튼은 "적용/갱신" 하나.
//   · 이펙트 없음 → 이펙트 적용 + 복제   · 있음 → '복제 개수'/'배치 모드'로 갱신
//   · '래스터라이즈' 체크 → 클론을 독립 레이어로 굳힘   · '복제 개수' 1 → 클론 제거

// 확장 폴더 안 파일의 절대 경로 (CEP 가 file:///C:/... URL 형태로 돌려주는 경우가 있어 일반 경로로 정규화)
function extPath(rel) {
  try {
    let ext = csInterface.getSystemPath(SystemPath.EXTENSION) || '';
    if (/^file:/i.test(ext)) ext = decodeURIComponent(ext.replace(/^file:\/{2,3}/i, ''));
    return (ext + '/' + rel).replace(/\\/g, '/');
  } catch (e) { return ''; }
}
function clonerFfxPath() { return extPath('jsx/BANG_Cloner.ffx'); }

// 자동 갱신: 소스/클론 선택 중 '복제 개수' ≠ 현재 클론 수 이거나 '래스터라이즈' 체크 → 값이 1초간 안정되면 applyCloner
(function initClonerAuto() {
  let last = null, stable = 0, busy = false;
  setInterval(() => {
    if (busy) return;
    if (document.getElementById('status-text').textContent.startsWith('Cloner')) return;
    busy = true;
    evalScript('clonerPollState()', (result) => {
      busy = false;
      try {
        const st = JSON.parse(result);
        if (!st.success || !st.active) { last = null; stable = 0; return; }
        const want = Math.max(1, st.count), need = st.bake || (want - 1 !== st.clones);
        const key = `${st.source}|${want}|${st.bake ? 1 : 0}`;
        if (!need) { last = key; stable = 0; return; }
        if (key === last) stable++; else { last = key; stable = 0; }
        if (stable >= 1) { stable = 0; applyScriptCloner(); }
      } catch (e) { /* ignore */ }
    });
  }, 700);
})();

// 네이티브 이펙트 적용 (BANG Cloner / BANG Stroke). 플러그인이 없으면 onMissing() (클로너는 스크립트 클로너로 폴백)
function applyNative(matchName, label, onMissing) {
  setStatus(label + '...');
  evalScript(`applyNativeEffect(${JSON.stringify(matchName)})`, (result) => {
    try {
      const res = JSON.parse(result);
      if (!res.success) { setStatus('Error: ' + res.error, 'error'); return; }
      if (res.missing) {
        if (onMissing) { onMissing(); return; }
        setStatus(`${label}: 네이티브 플러그인(${matchName}.aex)이 설치되어 있지 않습니다 — INSTALL.txt 2-1 참고`, 'error');
        return;
      }
      if (res.paths !== undefined) { setStatus(`Cloner Path: "${res.target}" 에 적용 — Layout = Path, Path Layer = "${res.shape}" (패스 ${res.paths}개를 따라 배치)`, 'success'); return; }
      const parts = [];
      if (res.added) parts.push(`${res.added}개 레이어에 적용`);
      if (res.kept) parts.push(`${res.kept}개는 이미 적용됨`);
      setStatus(`${label}: ${parts.join(', ') || '대상 없음'} — 설정은 Effect Controls 에서`, res.added || res.kept ? 'success' : 'error');
    } catch (e) {
      setStatus('Unexpected response', 'error');
    }
  });
}

// 스크립트 클로너 (Pseudo Effect "BANG 클로너" + 레이어 복제) — 네이티브 플러그인이 없을 때의 폴백
function applyScriptCloner() {
  setStatus('Cloner...');
  evalScript(`applyCloner(${JSON.stringify(clonerFfxPath())})`, (result) => {
    try {
      const res = JSON.parse(result);
      if (!res.success) { setStatus('Error: ' + res.error, 'error'); return; }
      const m = {
        created: `클로너 적용: "${res.source}" × ${res.count} — 개수·배치는 이펙트 'BANG 클로너'에서 바꾼 뒤 다시 클릭`,
        recloned: `클론 갱신: "${res.source}" × ${res.count}`,
        removed: `클론 제거: "${res.source}" (복제 개수 1)`,
        baked: `래스터라이즈: ${res.baked} clone(s) → 독립 레이어 (이펙트 제거)`
      };
      setStatus(m[res.action] || 'Cloner: done', 'success');
    } catch (e) {
      setStatus('Unexpected response', 'error');
    }
  });
}

// Cloner 타일: 네이티브 "BANG Cloner"(인스턴스 렌더, 타이밍 정확) 우선, 없으면 스크립트 클로너
document.getElementById('btn-cloner').addEventListener('click', () => applyNative('BANG Cloner', 'Cloner', applyScriptCloner));
document.getElementById('btn-stroke').addEventListener('click', () => applyNative('BANG Stroke', 'Stroke'));
document.getElementById('btn-gradient').addEventListener('click', () => applyNative('BANG Gradient', 'Gradient'));

// ── Bento Grid (BentoGrid.jsx 이식) ───────────────────────────

// Bento Grid: 동봉한 원본 BentoGrid.jsx 의 ScriptUI 팔레트를 연다 (이미 떠 있으면 앞으로)
document.getElementById('btn-bento').addEventListener('click', () => {
  const path = extPath('jsx/BentoGrid.jsx');
  setStatus('Bento Grid...');
  evalScript(`(function(){ try { $.evalFile(new File(${JSON.stringify(path)})); return "ok"; } catch (e) { return "ERR " + e.toString(); } })()`, (r) => {
    if (typeof r === 'string' && r.indexOf('ERR') === 0) setStatus('Bento Grid: ' + r.slice(4), 'error');
    else setStatus('Bento Grid 창을 열었습니다', 'success');
  });
});
