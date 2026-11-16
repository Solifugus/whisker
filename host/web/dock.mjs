// Docking in a real browser (design.md §5ag): host/linux/dock.c through the
// web host. A pane dragged by its caption from the left to the right edge,
// the drop bar seen on the canvas on the way, the program told; and the
// control, a pane dropped in the middle, left where it was.
//
//   node dock.mjs <whisker-dock-web binary> <dir for pictures>
//
// Derived from code.mjs.
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [bin, outDir] = process.argv.slice(2);
const PORT = 18094, DEBUG = 19336;
let fails = 0;
const ok = (c, what) => { console.log((c ? 'ok    ' : 'FAIL  ') + what); if (!c) fails++; };
const sleep = ms => new Promise(r => setTimeout(r, ms));

const app = spawn(bin, [], { env: { ...process.env, WHISKER_PORT: String(PORT) } });
let said = '';
app.stdout.on('data', d => { said += d; });
const profile = mkdtempSync(join(tmpdir(), 'whisker-chrome-'));
const chrome = spawn('google-chrome', ['--headless=new', `--remote-debugging-port=${DEBUG}`,
  `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--window-size=1100,700', 'about:blank'], { stdio: 'ignore' });

// however this ends -- a failure, ^C, the reader of its output going away --
// Chrome and the program go with it: a Chrome left running keeps its pages,
// which reconnect to the next run's program on the same port as extra seats
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
    await sleep(100);
  }
  return last;
}
async function at(x, y) {             // device pixels to page coordinates
  return js(`(()=>{const r=whisker.canvas.getBoundingClientRect();
    return [r.left+(${x}+0.5)*r.width/whisker.canvas.width, r.top+(${y}+0.5)*r.height/whisker.canvas.height];})()`);
}
async function key(name, code, modifiers = 0, commands) {   // CDP: Alt 1, Ctrl 2, Shift 8
  const vk = { Enter: 13, Tab: 9, Home: 36, End: 35, Backspace: 8, ArrowLeft: 37 }[name] ?? name.toUpperCase().charCodeAt(0);
  await cdp('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: name, code, modifiers,
                                        windowsVirtualKeyCode: vk, ...(commands ? { commands } : {}) });
  await cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: name, code, modifiers });
}
async function snap(name) {
  const url = await js('whisker.canvas.toDataURL("image/png")');
  writeFileSync(join(outDir, name), Buffer.from(url.split(',')[1], 'base64'));
  console.log(`      wrote ${join(outDir, name)}`);
}

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
  await cdp('Page.navigate', { url: `http://127.0.0.1:${PORT}/` });
  await sleep(500);
  ok((await settle()) > 0, 'the docked window draws in a real browser');
  await snap('dock_first.png');

  // the font is 18: a caption is 28 rows; the left area 204 wide (a fifth)
  const ACCENT = [0x2a, 0x6b, 0x8f];
  const px = (x, y) => js(`Array.from(whisker.canvas.getContext("2d").getImageData(${x},${y},1,1).data.slice(0,3))`);
  const isAccent = p => p.every((v, i) => Math.abs(v - ACCENT[i]) <= 2);
  const bar = async () => {             // the accent down the right edge's last 6 columns, mid-height
    let n = 0;
    for (let y = 60; y < 400; y += 20) if (isAccent(await px(1020, y))) n++;
    return n;
  };
  ok((await bar()) === 0, 'before a drag: no bar at the right edge');
  const lightIn = (x0, x1, y0, y1) => js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(${x0},${y0},${x1 - x0},${y1 - y0}).data;
    let n=0;for(let i=0;i<d.length;i+=4)if(d[i]+d[i+1]+d[i+2]>500)n++;return n;})()`);
  const rightBefore = await lightIn(820, 1000, 4, 24);

  // Files, dragged by its caption from the top left to the right edge
  const drag = async (x0, y0, x1, y1, during) => {
    const [ax, ay] = await at(x0, y0), [bx, by] = await at(x1, y1);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: ax, y: ay, button: 'left', buttons: 1, clickCount: 1 });
    for (let k = 1; k <= 10; k++)
      await cdp('Input.dispatchMouseEvent', { type: 'mouseMoved', x: ax + (bx - ax) * k / 10, y: ay + (by - ay) * k / 10, button: 'left', buttons: 1 });
    await sleep(300);
    await settle();
    if (during) await during();
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: bx, y: by, button: 'left', buttons: 0, clickCount: 1 });
    await sleep(300);
    await settle();
  };
  let shown = 0;
  await drag(40, 14, 1010, 250, async () => { shown = await bar(); await snap('dock_dragging.png'); });
  ok(shown >= 15, `dragged near the right edge: the drop bar there, in the accent (${shown} of 17 samples)`);
  ok(said.includes('Files docked at the right'), 'let go: the program says Files is docked at the right');
  ok((await bar()) === 0, 'and the bar gone');
  // the caption's title now at the top right: light pixels in x 820..1000 of its row
  const right = await lightIn(820, 1000, 4, 24);
  ok(right > 40 && rightBefore < 10, `drawn there: a caption's title at the top right (${right} light pixels; ${rightBefore} before)`);
  await snap('dock_right.png');

  // the control: Outline, now alone on the left, dropped in the middle
  await drag(40, 14, 512, 250);
  ok(said.includes('Outline let go in the middle: left where it was'), 'CONTROL: Outline dropped in the middle stays where it was');

  // the layout, as the program keeps it
  await key('s', 'KeyS', 2);
  await sleep(300);
  const line = (said.match(/layout kept: (.*)/) || [])[1] || '';
  console.log(`      ${line}`);
  ok(/files:R/.test(line) && /outline:L/.test(line) && /output:B/.test(line), 'Ctrl-S: the layout line has Files right, Outline left, Output at the bottom');
} finally {
  try { ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} docking check(s) failed` : '\nall docking checks passed');
process.exit(fails ? 1 : 0);
