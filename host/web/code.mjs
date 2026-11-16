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
import { mkdtempSync, writeFileSync, rmSync, readFileSync } from 'node:fs';
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
async function click(x, y) {
  const [px, py] = await at(x, y);
  await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: px, y: py, button: 'left', buttons: 1, clickCount: 1 });
  await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: px, y: py, button: 'left', buttons: 0, clickCount: 1 });
}
async function clicks(x, y, n) {     // a double- or triple-click, as Chrome counts them
  const [px, py] = await at(x, y);
  for (let c = 1; c <= n; c++) {
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: px, y: py, button: 'left', buttons: 1, clickCount: c });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: px, y: py, button: 'left', buttons: 0, clickCount: c });
  }
}
async function type(text) {
  for (const ch of text) {
    await cdp('Input.dispatchKeyEvent', { type: 'keyDown', key: ch, text: ch });
    await cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: ch });
  }
}
async function key(name, code, modifiers = 0, commands) {   // CDP: Alt 1, Ctrl 2, Shift 8
  const vk = { Enter: 13, Tab: 9, Home: 36, End: 35, Backspace: 8, ArrowLeft: 37 }[name] ?? name.toUpperCase().charCodeAt(0);
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
    for(let x=50;x<1024;x++)for(let y=0;y<18;y++){const i=(y*1024+x)*4;
      if(d[i]+d[i+1]+d[i+2]>300)return x;}return -1;})()`);
  const kw = await lightAt(rowY(9)), body = await lightAt(rowY(10));
  console.log(`      line 9 starts at x ${kw}, line 10 at x ${body}`);
  ok(kw > 0 && Math.abs((body - kw) - 4 * 9) <= 2,
     'indentation kept: line 10 starts four characters (36 px) right of line 9 -- runs of spaces are not collapsed');
  const near = (r, g, b) => js(`(()=>{const c=whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data;
    let n=0;for(let i=0;i<c.length;i+=4)if(Math.abs(c[i]-${r})<8&&Math.abs(c[i+1]-${g})<8&&Math.abs(c[i+2]-${b})<8)n++;return n;})()`);
  // inside ONE piece of text -- a comment -- a run of spaces is the page's
  // to keep: line 2 is a quote, ten spaces and 35 characters, 46 in all,
  // from x 4 + 27 + 18 + 6 = 55 (numbers, then the mark column, §5v step 4):
  // its last letter ends near 55 + 46 x 9 = 469, and collapsed it would end
  // 81 sooner, near 388 -- measured both ways (at 37, before marks: 450, 369)
  const lastLight = y0 => js(`(()=>{const g=whisker.canvas.getContext("2d");
    const d=g.getImageData(0,${y0},1024,18).data;
    for(let x=1023;x>=0;x--)for(let y=0;y<18;y++){const i=(y*1024+x)*4;
      if(d[i]+d[i+1]+d[i+2]>200)return x;}return -1;})()`);
  const end2 = await lastLight(rowY(2));
  console.log(`      line 2 ends at x ${end2}`);
  ok(end2 > 420, 'a run of spaces inside one comment kept, not collapsed to one');
  const blue = await near(0x56, 0x9c, 0xd6), green = await near(0x6a, 0x99, 0x55);
  console.log(`      ${blue} pixels in the keyword colour, ${green} in the comment colour`);
  ok(blue > 200 && green > 200, 'keywords and comments drawn in the colours the program gave them');

  // §5v step 3, typed rather than read (sample.bas is ASCII again, so the
  // desktop window can open it): at the end of line 3, an em dash -- three
  // bytes -- and an e-acute -- two -- are one column each, 9 px, or every
  // column after them would drift; two Backspaces take both back whole
  const line3 = readFileSync(sample, 'utf8').split('\n')[2];
  const end3 = await lastLight(rowY(3));
  ok(Math.abs(end3 - (55 + line3.length * 9)) <= 6,
     `CONTROL: an ASCII line ends at its characters' width, near ${55 + line3.length * 9}: ${end3}`);
  await click(1000, rowY(3) + 8);
  await settle(); await sleep(200);
  const base = await lastLight(rowY(3));
  await type('\u2014');
  await settle(); await sleep(300);
  const endD = await lastLight(rowY(3));
  await type('\u00e9');
  await settle(); await sleep(300);
  const endE = await lastLight(rowY(3));
  await key('Backspace', 'Backspace');
  await key('Backspace', 'Backspace');
  await settle(); await sleep(300);
  const endB = await lastLight(rowY(3));
  console.log(`      line 3 ends (caret) at ${base}; with an em dash ${endD}; and an e-acute ${endE}; after two Backspaces ${endB}`);
  ok(endD - base === 9, 'an em dash typed, three bytes, is one column: 9 px, not 27');
  ok(endE - endD === 9 && endB === base, 'an e-acute, one more; and two Backspaces take both back whole');

  // §5w: a real double-click on "powers" in line 9, "function powers(n)" --
  // the browser counts the clicks, the page sends the count, whisker
  // selects the word -- and Ctrl-C copies it through the program
  await clicks(55 + 11 * 9 + 4, rowY(9) + 8, 2);
  await settle(); await sleep(200);
  await key('c', 'KeyC', 2);
  await settle(); await sleep(300);
  ok(await js('whisker.clip()') === 'powers', `a double-click in Chrome selects "powers", and Ctrl-C copies it: "${await js('whisker.clip()')}"`);
  await clicks(55 + 11 * 9 + 4, rowY(9) + 8, 3);
  await settle(); await sleep(200);
  await key('c', 'KeyC', 2);
  await settle(); await sleep(300);
  ok(await js('whisker.clip()') === 'function powers(n)', 'a triple-click, its line');
  // and Ctrl-Shift-Left from the line's end, three times: ")" (marks are a
  // word of their own), then "n", then "(" -- "(n)" selected
  await key('End', 'End');
  for (let i = 0; i < 3; i++) await key('ArrowLeft', 'ArrowLeft', 2 | 8);
  await key('c', 'KeyC', 2);
  await settle(); await sleep(300);
  ok(await js('whisker.clip()') === '(n)', `Ctrl-Shift-Left three times from the end: ")" then "n" then "(" -- "${await js('whisker.clip()')}"`);

  // a click in the gutter by line 10
  await click(10, rowY(10) + 8);
  await settle(); await sleep(300);
  ok(said.includes('breakpoint on  line 10'), 'a click in the gutter by line 10 reached the program: breakpoint on line 10');
  // and is drawn: the program's red square in the mark column, x 4..22
  const red = row => js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(4,${row},18,18).data;
    let n=0;for(let i=0;i<d.length;i+=4)if(d[i]>200&&d[i+1]<60&&d[i+2]<60)n++;return n;})()`);
  const r10 = await red(rowY(10)), r9 = await red(rowY(9));
  console.log(`      red pixels in the mark column: line 10 ${r10}, line 9 ${r9}`);
  ok(r10 >= 64 && r9 === 0, 'the breakpoint is drawn: a red square by line 10, and none by line 9 (the control)');
  await click(10, rowY(10) + 8);
  await settle(); await sleep(300);
  ok(said.includes('breakpoint off line 10') && (await red(rowY(10))) === 0,
     'clicked again, it goes: the program cleared it and whisker drew the line unmarked');

  // §5x: the divider over the output pane, 600 - 4 - 120 - 6 = 470..476,
  // dragged 100 up by a real mouse: the output pane 220 high, the bar there
  {
    const [px, py] = await at(500, 473), [, qy] = await at(500, 373);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: px, y: py, button: 'left', buttons: 1, clickCount: 1 });
    for (let k = 1; k <= 5; k++)
      await cdp('Input.dispatchMouseEvent', { type: 'mouseMoved', x: px, y: py + (qy - py) * k / 5, button: 'left', buttons: 1 });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: px, y: qy, button: 'left', buttons: 0, clickCount: 1 });
    await settle(); await sleep(300);
    const bar = y => js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(500,${y},1,1).data;return [d[0],d[1],d[2]].join(",");})()`);
    const at373 = await bar(373), at473 = await bar(473);
    console.log(`      the bar's colour 0x3a5068 is 58,80,104; at y 373: ${at373}, at y 473: ${at473}`);
    ok(said.includes('output pane 220 high') && at373 === '58,80,104' && at473 !== '58,80,104',
       'a real drag of the divider: the output pane 220 high, the bar moved up 100 px');
  }

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

  // §5ae: the chooser. The dialog is 640 x 440, centred at 192,80; its name
  // field the 30 rows above its buttons, 438..468
  {
    const saved = join(outDir, 'saved.bas'), other = join(outDir, 'other.bas');
    rmSync(saved, { force: true });
    writeFileSync(other, 'print 42\n');
    await key('S', 'KeyS', 2 | 8);                      // Ctrl-Shift-S: Save as
    await sleep(400);
    ok(said.includes('choose a name to save as'), 'Ctrl-Shift-S opens the chooser to save as');
    await snap('code_chooser.png');
    await click(500, 453);
    await key('a', 'KeyA', 2);                          // the name offered, all of it
    await type(saved);
    await key('Enter', 'Enter');
    await sleep(500);
    let written = '';
    try { written = readFileSync(saved, 'utf8'); } catch {}
    ok(said.includes('saved ' + saved) && written.includes('-- typed'),
       'a path typed over the name offered, Enter: the file written, with what was typed in it');
    await key('o', 'KeyO', 2);                          // Ctrl-O: open
    await sleep(400);
    await click(500, 453);
    await type(other);
    await key('Enter', 'Enter');
    await sleep(500);
    await settle();
    const shows = await js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(60,40,300,30).data;
      let n=0;for(let i=0;i<d.length;i+=4)if(d[i]>200&&d[i+1]>200&&d[i+2]>200)n++;return n;})()`);
    ok(said.includes('opened ' + other), 'Ctrl-O, a path typed, Enter: that file opened');
    await snap('code_opened.png');
    const before = (said.match(/opened /g) || []).length;
    await key('o', 'KeyO', 2);
    await sleep(300);
    await key('Escape', 'Escape');
    await sleep(400);
    ok(said.includes('nothing chosen') && (said.match(/opened /g) || []).length === before,
       'CONTROL: Ctrl-O then Escape: nothing chosen, nothing opened');
    // Down past the bottom of the list: the choice is always seen
    await key('o', 'KeyO', 2);
    await sleep(300);
    await key('Tab', 'Tab'); await key('Tab', 'Tab');
    for (let i = 0; i < 12; i++) {
      await cdp('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40 });
      await cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: 'ArrowDown', code: 'ArrowDown' });
    }
    await sleep(400);
    await settle();
    const chosen = await js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(200,166,624,264).data;
      let n=0;for(let i=0;i<d.length;i+=4)if(Math.abs(d[i]-0xb0)<4&&Math.abs(d[i+1]-0x6a)<4&&Math.abs(d[i+2]-0x2a)<4)n++;return n;})()`);
    await snap('code_chooser_down.png');
    ok(chosen > 2000, `Down twelve times, past the list's bottom: the chosen row is drawn, in view (${chosen} pixels of it)`);
    await key('Escape', 'Escape');
    await sleep(300);

    // §5af: a second window, the scratch pad -- a panel over the page
    await key('n', 'KeyN', 1);                          // Alt-N: a browser keeps Ctrl-N
    await sleep(500);
    await settle();
    const panels = await js('whisker.panels()');
    ok(said.includes('scratch opened') && panels.includes(1), `Alt-N: a second window, a panel over the page (windows ${panels})`);
    await js(`window.__p=whisker.panel(1).getContext("2d").getImageData(0,0,600,360).data.slice();1`);
    await type('hello');
    await sleep(400);
    await settle();
    const typedIn = await js(`(()=>{const d=whisker.panel(1).getContext("2d").getImageData(0,0,600,360).data;
      let n=0;for(let i=0;i<d.length;i+=4)if(d[i]!==__p[i]||d[i+1]!==__p[i+1]||d[i+2]!==__p[i+2])n++;return n;})()`);
    ok(typedIn > 100, `typing goes to the window in front -- the pad changed by ${typedIn} pixels`);
    await snap('code_windows.png');
    const at0 = await js(`(()=>{const d=document.querySelector('.win');return [d.offsetLeft,d.offsetTop];})()`);
    const bar = await js(`(()=>{const r=document.querySelector('.bar').getBoundingClientRect();return [r.left+60,r.top+12];})()`);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: bar[0], y: bar[1], button: 'left', buttons: 1, clickCount: 1 });
    for (let k = 1; k <= 5; k++)
      await cdp('Input.dispatchMouseEvent', { type: 'mouseMoved', x: bar[0] + 20 * k, y: bar[1] + 10 * k, button: 'left', buttons: 1 });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: bar[0] + 100, y: bar[1] + 50, button: 'left', buttons: 0, clickCount: 1 });
    await sleep(200);
    const at1 = await js(`(()=>{const d=document.querySelector('.win');return [d.offsetLeft,d.offsetTop];})()`);
    ok(at1[0] - at0[0] === 100 && at1[1] - at0[1] === 50, `dragged by its title bar: moved ${at1[0]-at0[0]},${at1[1]-at0[1]}`);
    await click(950, 560);                              // back on the editor, clear of the panel
    await key('s', 'KeyS', 2);
    await sleep(300);
    ok(said.includes('save: only Save as'), 'a click on the editor, then Ctrl-S: the editor has the keys again');
    const x = await js(`(()=>{const r=document.querySelector('.bar b').getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2];})()`);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: x[0], y: x[1], button: 'left', buttons: 1, clickCount: 1 });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: x[0], y: x[1], button: 'left', buttons: 0, clickCount: 1 });
    await sleep(500);
    const left = await js('whisker.panels()');
    ok(said.includes('scratch closed') && !left.includes(1) && !said.includes('editor closed'),
       'its close button: the program closes the pad -- and the editor goes on');
    void shows;
  }
} finally {
  try { ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} code-editor check(s) failed` : '\nall code-editor checks passed');
process.exit(fails ? 1 : 0);
