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

const CP_HISTORY_KEY = 'aegreatagain-cp-history';
const CP_HISTORY_MAX = 8;
let cpCurrentHex = '#4CAF50';

// ── 유틸리티 ─────────────────────────────────────────────────

// hex → {r, g, b}
function cpHexToRgb(hex) {
  const h = hex.replace('#', '');
  return {
    r: parseInt(h.slice(0, 2), 16),
    g: parseInt(h.slice(2, 4), 16),
    b: parseInt(h.slice(4, 6), 16)
  };
}

// hex → {h, s, b}  (HSB / HSV)
function cpHexToHsb(hex) {
  const { r, g, b } = cpHexToRgb(hex);
  const rn = r / 255, gn = g / 255, bn = b / 255;
  const max = Math.max(rn, gn, bn);
  const min = Math.min(rn, gn, bn);
  const delta = max - min;

  let h = 0;
  if (delta !== 0) {
    if      (max === rn) h = ((gn - bn) / delta) % 6;
    else if (max === gn) h = (bn - rn) / delta + 2;
    else                 h = (rn - gn) / delta + 4;
    h = Math.round(h * 60);
    if (h < 0) h += 360;
  }
  return {
    h: h,
    s: max === 0 ? 0 : Math.round((delta / max) * 100),
    b: Math.round(max * 100)
  };
}

// hex 문자열 정규화 (#RRGGBB 형식 보장, null 반환 시 유효하지 않음)
function cpNormHex(raw) {
  let h = String(raw).replace(/[^0-9a-fA-F]/g, '');
  if (h.length === 3) h = h[0]+h[0]+h[1]+h[1]+h[2]+h[2];
  if (h.length !== 6) return null;
  return '#' + h.toUpperCase();
}

// ── UI 전체 갱신 ──────────────────────────────────────────────

function cpUpdateUI(hex) {
  cpCurrentHex = hex;
  const upper = hex.replace('#', '').toUpperCase();
  const { r, g, b }       = cpHexToRgb(hex);
  const { h, s, b: bri }  = cpHexToHsb(hex);
  document.getElementById('cp-preview').style.background = hex;
  document.getElementById('cp-hex-val').textContent = upper;
  document.getElementById('cp-r').textContent   = r;
  document.getElementById('cp-g').textContent   = g;
  document.getElementById('cp-b').textContent   = b;
  document.getElementById('cp-hue').textContent = h;
  document.getElementById('cp-sat').textContent = s;
  document.getElementById('cp-bri').textContent = bri;
}

// ── 히스토리 ─────────────────────────────────────────────────

function cpLoadHistory() {
  try { return JSON.parse(localStorage.getItem(CP_HISTORY_KEY) || '[]'); }
  catch { return []; }
}

function cpAddToHistory(hex) {
  let h = cpLoadHistory();
  // 중복 제거 후 최신 색상을 앞에 추가, 최대 8개 유지
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
    btn.title            = '#' + hex.replace('#', '').toUpperCase();
    btn.addEventListener('click', () => {
      cpUpdateUI(hex);
      const copyFallback = () => {
        try {
          const ta = document.createElement('textarea');
          ta.value = hex;
          document.body.appendChild(ta);
          ta.select();
          document.execCommand('copy');
          document.body.removeChild(ta);
          setStatus('Copied: ' + hex, 'success');
        } catch (e) {
          setStatus('Color: ' + hex, 'default');
        }
      };
      if (navigator.clipboard) {
        navigator.clipboard.writeText(hex)
          .then(() => setStatus('Copied: ' + hex, 'success'))
          .catch(copyFallback);
      } else {
        copyFallback();
      }
    });
    container.appendChild(btn);
  });

  // 빈 슬롯 채우기
  for (let i = history.length; i < CP_HISTORY_MAX; i++) {
    const slot = document.createElement('div');
    slot.className = 'cp-swatch cp-swatch--empty';
    container.appendChild(slot);
  }
}

// ── AE 네이티브 Color Picker ──────────────────────────────────
//
//  [Primary]  evalScript → openAEColorPicker() (hostscript.jsx)
//             임시 Null + Color Control + executeCommand(2240) 기법으로
//             AE 네이티브 컬러 피커(eyedropper 포함)를 동기적으로 연다.
//
//  [Fallback] 활성 컴프 없을 때 → <input type="color"> 폴백
//             Chromium 내장 컬러 피커 다이얼로그를 열어 기본 색상 선택.

const colorInput    = document.getElementById('cp-color-input');
const eyedropperBtn = document.getElementById('cp-eyedropper-btn');
const cpStatusEl    = document.getElementById('cp-status');
let   pickActive    = false;

function cpApplyPickedHex(hex) {
  cpUpdateUI(hex);
  cpAddToHistory(hex);
  setStatus('Picked: ' + hex, 'success');
}

function cpResetPickState() {
  pickActive = false;
  eyedropperBtn.classList.remove('cp-pick-btn--active');
  cpStatusEl.textContent = '';
}

eyedropperBtn.addEventListener('click', () => {
  if (pickActive) return;
  pickActive = true;
  eyedropperBtn.classList.add('cp-pick-btn--active');
  cpStatusEl.textContent = 'Opening color picker...';
  setStatus('Opening color picker...');

  const initialHex = cpCurrentHex.replace('#', '');

  csInterface.evalScript('openAEColorPicker("' + initialHex + '")', (res) => {
    cpResetPickState();

    let r;
    try { r = JSON.parse(res); } catch (e) { r = { success: false, error: String(res) }; }

    if (!r.success) {
      const msg = r.error || '';
      cpStatusEl.textContent = 'Error: ' + msg;
      setStatus('Color picker error', 'error');
      return;
    }

    const hex = cpNormHex(r.hex);
    if (hex && hex !== cpCurrentHex) {
      cpApplyPickedHex(hex);
    } else {
      setStatus('Cancelled', 'default');
    }
  });
});

// ── 클립보드 복사 ─────────────────────────────────────────────

document.getElementById('cp-copy-btn').addEventListener('click', () => {
  const hex = cpCurrentHex;
  (navigator.clipboard
    ? navigator.clipboard.writeText(hex)
    : Promise.reject()
  ).catch(() => {
    // clipboard API 미지원 폴백
    const ta = document.createElement('textarea');
    ta.value = hex;
    document.body.appendChild(ta);
    ta.select();
    document.execCommand('copy');
    document.body.removeChild(ta);
  }).then(() => setStatus('Copied: ' + hex, 'success'))
    .catch(() => setStatus('Copy failed', 'error'));
});

// ── 히스토리 초기화 ───────────────────────────────────────────

document.getElementById('cp-clear-btn').addEventListener('click', () => {
  localStorage.removeItem(CP_HISTORY_KEY);
  cpRenderHistory();
  setStatus('History cleared');
});

// ── 초기화 ───────────────────────────────────────────────────

cpUpdateUI('#4CAF50');
cpRenderHistory();

// ── Green Null Creator ────────────────────────────────────────

document.getElementById('btn-create-green-null').addEventListener('click', () => {
  setStatus('Creating Green Null...');
  evalScript('createGreenNull()', (result) => {
    try {
      const res = JSON.parse(result);
      if (res.success) {
        const msg = res.parented > 0
          ? `"${res.name}" created — ${res.parented} layer(s) parented`
          : `"${res.name}" created`;
        setStatus(msg, 'success');
      } else {
        setStatus('Error: ' + res.error, 'error');
      }
    } catch (e) {
      setStatus('Unexpected response', 'error');
    }
  });
});
