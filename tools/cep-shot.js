#!/usr/bin/env node
// cep-shot.js — save a PNG screenshot of the running panel (CEP remote debugging, port 8089).
//   node tools/cep-shot.js <out.png>
'use strict';
const out = process.argv[2] || 'panel.png';
(async () => {
  const pages = await (await fetch('http://localhost:' + (process.env.CEP_DEBUG_PORT || 8089) + '/json')).json();
  const page = pages.find(p => /BANG_Toolbox|com\.bang\.toolbox/.test(p.title + p.url));
  if (!page) throw new Error('panel not found');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  const res = await new Promise(r => { ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id === 1) r(m.result); };
    ws.send(JSON.stringify({ id: 1, method: 'Page.captureScreenshot', params: { format: 'png' } })); });
  require('fs').writeFileSync(out, Buffer.from(res.data, 'base64'));
  console.log('saved', out);
  ws.close();
})().catch(e => { console.error('ERROR:', e.message); process.exit(1); });
