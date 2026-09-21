#!/usr/bin/env node
/**
 * cep-eval.js — talk to the running BANG_Toolbox panel through CEP remote debugging.
 *
 * Requires: AE running with the panel open and `.debug` deployed (port 8089).
 *
 *   node tools/cep-eval.js js  "<javascript run in the panel page>"
 *   node tools/cep-eval.js jsx "<ExtendScript run via csInterface.evalScript>"
 *   node tools/cep-eval.js status            # panel status-bar text + class
 *   node tools/cep-eval.js click <buttonId>  # e.g. btn-quote-align, btn-create-green-null
 *
 * Output is JSON. Node >= 22 (global WebSocket).
 */
'use strict';

const PORT = process.env.CEP_DEBUG_PORT || 8089;

async function getPage() {
  const res = await fetch(`http://localhost:${PORT}/json`);
  const pages = await res.json();
  const page = pages.find(p => p.type === 'page' && /BANG_Toolbox|com\.bang\.toolbox/.test(p.title + p.url));
  if (!page) throw new Error('BANG_Toolbox page not found on port ' + PORT + '. Is the panel open?');
  return page;
}

function connect(url) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    ws.onopen = () => resolve(ws);
    ws.onerror = e => reject(new Error('websocket error: ' + (e.message || e.type)));
  });
}

async function main() {
  const [mode, ...rest] = process.argv.slice(2);
  if (!mode) { console.error('usage: cep-eval.js js|jsx|status|click <arg>'); process.exit(2); }

  let expression;
  switch (mode) {
    case 'js':
      expression = rest.join(' ');
      break;
    case 'jsx':
      expression = `new Promise(r => csInterface.evalScript(${JSON.stringify(rest.join(' '))}, r))`;
      break;
    case 'status':
      expression = `(() => { const el = document.getElementById('status-text'); return { text: el && el.textContent, className: el && el.className }; })()`;
      break;
    case 'click':
      expression = `(() => { const b = document.getElementById(${JSON.stringify(rest[0])}); if (!b) return 'no such button'; b.click(); return 'clicked ' + b.id; })()`;
      break;
    default:
      console.error('unknown mode ' + mode); process.exit(2);
  }

  const page = await getPage();
  const ws = await connect(page.webSocketDebuggerUrl);
  const result = await new Promise((resolve, reject) => {
    const id = 1;
    const timer = setTimeout(() => reject(new Error('timeout (a modal alert in AE blocks evalScript until dismissed)')), 120000);
    ws.onmessage = ev => {
      const msg = JSON.parse(ev.data);
      if (msg.id !== id) return;
      clearTimeout(timer);
      resolve(msg);
    };
    ws.send(JSON.stringify({
      id,
      method: 'Runtime.evaluate',
      params: { expression, awaitPromise: true, returnByValue: true }
    }));
  });
  ws.close();

  if (result.error) { console.log(JSON.stringify({ error: result.error }, null, 2)); process.exit(1); }
  const r = result.result;
  if (r.exceptionDetails) { console.log(JSON.stringify({ exception: r.exceptionDetails.exception?.description || r.exceptionDetails.text }, null, 2)); process.exit(1); }
  console.log(JSON.stringify(r.result.value, null, 2));
}

main().catch(e => { console.error('ERROR:', e.message); process.exit(1); });
