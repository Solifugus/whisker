// Tabs in a real browser (design.md §5ah): host/linux/tabs.c through the web
// host. A tab chosen; a background tab's close button closing it without
// switching to it (STU-21); an unsaved note asking first; a right-click's
// Close others acting on the tab clicked, not the current one.
//
//   node tabs.mjs <whisker-tabs-web binary> <dir for pictures>
//
// Derived from dock.mjs.

import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [bin, outDir] = process.argv.slice(2);
const PORT = 18095, DEBUG = 19337;
let fails = 0;
const ok = (c, what) => { console.log((c ? 'ok    ' : 'FAIL  ') + what); if (!c) fails++; };
const sleep = ms => new Promise(r => setTimeout(r, ms));

const app = spawn(bin, [], { env: { ...process.env, WHISKER_PORT: String(PORT) } });
let said = '';
app.stdout.on('data', d => { said += d; });
const profile = mkdtempSync(join(tmpdir(), 'whisker-chrome-'));
const chrome = spawn('google-chrome', ['--headless=new', `--remote-debugging-port=${DEBUG}`,
  `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--window-size=1200,700', 'about:blank'], { stdio: 'ignore' });

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
  ok((await settle()) > 0, 'the tabbed window draws in a real browser');
  ok(said.includes('Note 1 made') && said.includes('Note 3 made'), 'three notes, a tab each');

  const ACCENT = [0x2a, 0x6b, 0x8f];
  const press = async (x, y, button = 'left') => {
    const [ax, ay] = await at(x, y);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: ax, y: ay, button, buttons: button === 'left' ? 1 : 2, clickCount: 1 });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: ax, y: ay, button, buttons: 0, clickCount: 1 });
    await sleep(400);
    await settle();
  };
  // the close buttons along the bar, left to right: runs of the accent in row 14
  const xs = async () => {
    const row = await js(`Array.from(whisker.canvas.getContext("2d").getImageData(0,14,1024,1).data)`);
    const runs = [];
    let from = -1;
    for (let x = 0; x <= 1024; x++) {
      const on = x < 1024 && ACCENT.every((v, i) => Math.abs(row[4 * x + i] - v) <= 2);
      if (on && from < 0) from = x;
      if (!on && from >= 0) { if (x - from > 10) runs.push(Math.floor((from + x) / 2)); from = -1; }
    }
    return runs;
  };
  let x = await xs();
  ok(x.length === 3, `three close buttons on the bar (at ${x})`);
  await snap('tabs_first.png');

  // Note 1 tapped by its caption: chosen
  await press(20, 14);
  ok(said.includes('Note 1 chosen'), 'Note 1 tapped: chosen');
  await key('h', 'KeyH'); await key('i', 'KeyI');
  await sleep(300); await settle();

  // STU-21: Note 2's close button, in the background -- closed, Note 1 still chosen
  x = await xs();
  const before = said.length;
  await press(x[1], 14);
  const after = said.slice(before);
  ok(after.includes('Note 2 closed') && !after.includes('chosen'), `Note 2's close button tapped in the background: closed, nothing chosen (${JSON.stringify(after.trim())})`);
  await key('s', 'KeyS', 2);
  await sleep(300);
  ok(said.includes('Note 1 saved'), 'and Ctrl-S saves Note 1 -- still the current note');

  // an unsaved note asks first
  await key('!', 'Digit1', 8);
  await sleep(300); await settle();
  x = await xs();
  await press(x[0], 14);
  ok(said.includes('Note 1 is not saved') && !said.includes('Note 1 closed'), 'typed in, then its close button: asked first, not closed');
  await press(x[0], 14);
  ok(said.includes('Note 1 closed'), 'its close button again: closed');

  // a right-click on a background tab: the menu acts on it, never switching
  // Alt-T: a browser never gives a page Ctrl-T (nor Ctrl-W), so Alt does it
  await key('t', 'KeyT', 1); await sleep(200);
  await key('t', 'KeyT', 1); await sleep(400); await settle();
  ok(said.includes('Note 4 made') && said.includes('Note 5 made'), 'Alt-T twice: Note 4 and Note 5');
  const b2 = said.length;
  await press(20, 14, 'right');                      // Note 3's tab, the first now
  ok(said.slice(b2).includes('a menu on Note 3'), 'a right-click on Note 3, in the background: its menu');
  await snap('tabs_menu.png');
  // the menu hangs from the pointer: Close, then Close others under it
  await press(20 + 60, 14 + 6 + 40 + 4 + 20);
  const got = said.slice(b2);
  ok(got.includes('Note 4 closed') && got.includes('Note 5 closed') && !got.includes('Note 3 closed') && !got.includes('Note 3 chosen'),
     `Close others: Note 4 and Note 5 closed, Note 3 kept -- and never chosen first (${JSON.stringify(got.trim())})`);
  x = await xs();
  ok(x.length === 1, `one tab left (close buttons at ${x})`);
  await snap('tabs_last.png');
  await key('w', 'KeyW', 1);
  await sleep(400); await settle();
  ok(said.includes('Note 3 closed') && (await xs()).length === 0, 'Alt-W: the last note closed, no tab left');
} finally {
  try { ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} tabs check(s) failed` : '\nall tabs checks passed');
process.exit(fails ? 1 : 0);
