// Each seat repaints only what changed for it (design.md §5ab step 3):
// people wander under partial repaints, then stop, and the canvas those
// repaints left must equal a full redraw, pixel for pixel -- a row missed
// would be a smear left behind.
//
//   node repaint.mjs <whisker-floor-web binary>
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [bin] = process.argv.slice(2);
const PORT = 18097, DEBUG = 19337;
let fails = 0;
const ok = (c, what) => { console.log((c ? 'ok    ' : 'FAIL  ') + what); if (!c) fails++; };
const sleep = ms => new Promise(r => setTimeout(r, ms));

const app = spawn(bin, [], { env: { ...process.env, WHISKER_PORT: String(PORT), WHISKER_FLOOR_SETTLE: '5' } });
let said = '';
app.stdout.on('data', d => { said += d; });
const profile = mkdtempSync(join(tmpdir(), 'whisker-chrome-'));
const chrome = spawn('google-chrome', ['--headless=new', `--remote-debugging-port=${DEBUG}`,
  `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--window-size=1100,700', 'about:blank'], { stdio: 'ignore' });

// however this ends, Chrome and the program go with it
const reap = () => { try { chrome.kill('SIGKILL'); } catch {} try { app.kill('SIGKILL'); } catch {} };
process.on('exit', reap);
for (const sig of ['SIGINT', 'SIGTERM', 'SIGHUP', 'SIGPIPE']) process.on(sig, () => { reap(); process.exit(1); });

let ws, seq = 0;
const waiting = new Map();
function cdp(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise(res => waiting.set(id, res));
}
async function js(expr) {
  const r = await cdp('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
  return r.result?.value;
}
async function settle() {             // every document drawn, and no more coming
  let last = -1;
  for (let i = 0; i < 40; i++) {
    const n = await js('whisker.idle().then(()=>whisker.drawn())');
    if (n === last) return n;
    last = n;
    await sleep(150);
  }
  return last;
}
const keep = name => js(`(()=>{window['${name}']=whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data.slice();return 1;})()`);
const differ = (a, b) => js(`(()=>{const x=window['${a}'],y=window['${b}'];let n=0;
  for(let i=0;i<x.length;i+=4)if(x[i]!==y[i]||x[i+1]!==y[i+1]||x[i+2]!==y[i+2])n++;return n;})()`);

try {
  let target;
  for (let i = 0; i < 50 && !target; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${DEBUG}/json/list`)).json();
      target = list.find(t => t.type === 'page');
    } catch { await sleep(100); }
  }
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise(r => ws.addEventListener('open', r));
  ws.addEventListener('message', e => {
    const m = JSON.parse(e.data);
    if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m.result ?? m); waiting.delete(m.id); }
  });
  await cdp('Page.enable');
  await cdp('Runtime.enable');
  await cdp('Emulation.setDeviceMetricsOverride', { width: 1024, height: 600, deviceScaleFactor: 1, mobile: false });
  // count the documents that are not the whole window, as they arrive
  await cdp('Page.addScriptToEvaluateOnNewDocument', { source: `window.partial=0;window.whole=0;
    const ES=window.EventSource;window.EventSource=function(u){const e=new ES(u);
      e.addEventListener('message',m=>{const v=/^(\\d+) (\\d+) /.exec(m.data);
        if(v){if(+v[1]===0&&(+v[2]===0||+v[2]===600))window.whole++;else window.partial++;}});return e;};` });
  await cdp('Page.navigate', { url: `http://127.0.0.1:${PORT}/` });
  await sleep(2500);
  await keep('walking');
  for (let i = 0; i < 80 && !said.includes('still  everyone has stopped'); i++) await sleep(250);
  ok(said.includes('still  everyone has stopped'), 'people wandered for five seconds, then everyone stopped');
  await settle();
  const counts = await js('[window.partial, window.whole]');
  console.log(`      while they walked: ${counts[0]} documents of only what changed, ${counts[1]} of the whole window`);
  ok(counts[0] > 20, 'and the page was kept by partial repaints, not whole ones');
  await keep('kept');
  await js('whisker.send("X")');              // the program draws this seat whole
  await sleep(500);
  await settle();
  await keep('full');
  const d = await differ('kept', 'full');
  ok(d === 0, `the canvas the repaints left equals a full redraw: ${d} pixels differ`);
  const c = await differ('walking', 'kept');
  ok(c > 1000, `CONTROL: the canvas mid-walk differs from the still one -- ${c} pixels -- so a smear would be seen`);
} finally {
  reap();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} repaint check(s) failed` : '\nall repaint checks passed');
process.exit(fails ? 1 : 0);
