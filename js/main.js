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

const CP_HISTORY_KEY = 'bang-toolbox-cp-history';
const CP_HISTORY_MAX = 12;
let cpCurrentHex = '#4CAF50';

// 리브랜딩: 구 키(aegreatagain-cp-history)에 저장된 히스토리를 신규 키로 1회 이관.
// 기존 저장 색상 스와치를 잃지 않도록 보존한 뒤 구 키를 제거한다.
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
    btn.setAttribute('aria-label', btn.title);
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
          setStatus('Copied: ' + hex, 'success'); cpToast('복사 완료!');
        } catch (e) {
          setStatus('Color: ' + hex, 'default');
        }
      };
      if (navigator.clipboard) {
        navigator.clipboard.writeText(hex)
          .then(() => { setStatus('Copied: ' + hex, 'success'); cpToast('복사 완료!'); })
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

// 미리보기 위에 "복사 완료!" 를 1.2초 표시
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
  }).then(() => { setStatus('Copied: ' + hex, 'success'); cpToast('복사 완료!'); })
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
