// The code editor in a real browser (design.md §5v): host/linux/code.c on
// sample.bas, through the web host. What a person would check by eye, asked
// of the canvas: indentation kept, keywords and comments in their colours,
// a gutter click reaching the program, and after typing, the canvas built
// from clipped repaints equal to a full redraw.
//
//   node code.mjs <whisker-code-web binary> <sample.bas> <dir for pictures>
//
// Derived from chrome.mjs, whose header follows.
//
// The web host in a real browser (design.md §5k, criteria 3 and 4).
//
// Headless Chrome in a throwaway profile, driven over its DevTools protocol
// with nothing but Node's own WebSocket: it loads the page edit.c is served
// on, types into the focused field, clicks the second field and types
// there, presses Enter -- as a person would, through real pointer and key
// events. Then the check only a browser can make: the canvas after all those
// CLIPPED repaints is compared with the canvas after a FULL redraw, pixel for
// pixel -- the board's banded rule, kept by the page.
//
//   node chrome.mjs <whisker-edit-web binary> <dir for pictures>
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [bin, sample, outDir] = process.argv.slice(2);
const PORT = 18092, DEBUG = 19334;
let fails = 0;
const ok = (c, what) => { console.log((c ? 'ok    ' : 'FAIL  ') + what); if (!c) fails++; };
const sleep = ms => new Promise(r => setTimeout(r, ms));

const app = spawn(bin, [sample], { env: { ...process.env, WHISKER_PORT: String(PORT) } });
let said = '';
app.stdout.on('data', d => { said += d; });
const profile = mkdtempSync(join(tmpdir(), 'whisker-chrome-'));
const chrome = spawn('google-chrome', ['--headless=new', `--remote-debugging-port=${DEBUG}`,
  `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--window-size=1100,700', 'about:blank'], { stdio: 'ignore' });

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
async function click(x, y) {
  const [px, py] = await at(x, y);
  await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: px, y: py, button: 'left', buttons: 1, clickCount: 1 });
  await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: px, y: py, button: 'left', buttons: 0, clickCount: 1 });
}
async function type(text) {
  for (const ch of text) {
    await cdp('Input.dispatchKeyEvent', { type: 'keyDown', key: ch, text: ch });
    await cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: ch });
  }
}
async function key(name, code, modifiers = 0, commands) {   // CDP: Alt 1, Ctrl 2, Shift 8
  const vk = { Enter: 13, Tab: 9, Home: 36, End: 35 }[name] ?? name.toUpperCase().charCodeAt(0);
  await cdp('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: name, code, modifiers,
                                        windowsVirtualKeyCode: vk, ...(commands ? { commands } : {}) });
  await cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: name, code, modifiers });
}
async function rightClick(x, y) {
  const [px, py] = await at(x, y);
  await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: px, y: py, button: 'right', buttons: 2, clickCount: 1 });
  await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: px, y: py, button: 'right', buttons: 0, clickCount: 1 });
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
  ok((await settle()) > 0, 'the code editor draws in a real browser');
  await snap('code_first.png');

  // the font is 18: a character 9 wide, rows 22 apart from 4 + 32 + 6.
  // Line 9 of sample.bas is "function powers(n)", line 10 "    rows = []".
  const rowY = k => 4 + 32 + 6 + (k - 1) * 22;
  const lightAt = y0 => js(`(()=>{const g=whisker.canvas.getContext("2d");
    const d=g.getImageData(0,${y0},1024,18).data;
    for(let x=40;x<1024;x++)for(let y=0;y<18;y++){const i=(y*1024+x)*4;
      if(d[i]+d[i+1]+d[i+2]>300)return x;}return -1;})()`);
  const kw = await lightAt(rowY(9)), body = await lightAt(rowY(10));
  console.log(`      line 9 starts at x ${kw}, line 10 at x ${body}`);
  ok(kw > 0 && Math.abs((body - kw) - 4 * 9) <= 2,
     'indentation kept: line 10 starts four characters (36 px) right of line 9 -- runs of spaces are not collapsed');
  const near = (r, g, b) => js(`(()=>{const c=whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data;
    let n=0;for(let i=0;i<c.length;i+=4)if(Math.abs(c[i]-${r})<8&&Math.abs(c[i+1]-${g})<8&&Math.abs(c[i+2]-${b})<8)n++;return n;})()`);
  // inside ONE piece of text -- a comment -- a run of spaces is the page's
  // to keep: line 2 is a quote, ten spaces and 35 characters, 46 in all,
  // from x 4 + 27 + 6 = 37: its last letter ends near 37 + 46 x 9 = 451,
  // and collapsed it would end 81 sooner, near 370 -- measured both ways
  const lastLight = y0 => js(`(()=>{const g=whisker.canvas.getContext("2d");
    const d=g.getImageData(0,${y0},1024,18).data;
    for(let x=1023;x>=0;x--)for(let y=0;y<18;y++){const i=(y*1024+x)*4;
      if(d[i]+d[i+1]+d[i+2]>200)return x;}return -1;})()`);
  const end2 = await lastLight(rowY(2));
  console.log(`      line 2 ends at x ${end2}`);
  ok(end2 > 400, 'a run of spaces inside one comment kept, not collapsed to one');
  const blue = await near(0x56, 0x9c, 0xd6), green = await near(0x6a, 0x99, 0x55);
  console.log(`      ${blue} pixels in the keyword colour, ${green} in the comment colour`);
  ok(blue > 200 && green > 200, 'keywords and comments drawn in the colours the program gave them');

  // a click in the gutter by line 10
  await click(10, rowY(10) + 8);
  await settle(); await sleep(300);
  ok(said.includes('breakpoint on  line 10'), 'a click in the gutter by line 10 reached the program: breakpoint on line 10');

  // type at the end of line 1, a comment: still green, and the banded rule
  await click(1000, rowY(1) + 8);
  await type(' -- typed');
  await settle(); await sleep(300);
  await snap('code_typed.png');
  await js('window.__a = whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data.slice(); true');
  const before = await settle();
  await js('whisker.send("X"); true');
  await sleep(400);
  const after = await settle();
  ok(after > before, 'asked to draw everything again, the program sent a full document');
  const diff = await js(`(()=>{const b=whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data;
    let n=0; for(let i=0;i<b.length;i+=4) if(b[i]!==__a[i]||b[i+1]!==__a[i+1]||b[i+2]!==__a[i+2]) n++; return n;})()`);
  ok(diff === 0, `after typing, the canvas from clipped repaints equals a full redraw (${diff} differ)`);
} finally {
  try { ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} code-editor check(s) failed` : '\nall code-editor checks passed');
process.exit(fails ? 1 : 0);
