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

const [bin, outDir] = process.argv.slice(2);
const PORT = 18091, DEBUG = 19333;
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
  // the clipboard, as a person who allowed it (§5s); said, if refused
  const perm = await cdp('Browser.grantPermissions', { origin: `http://127.0.0.1:${PORT}`,
    permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });
  const granted = !(perm && perm.error);
  if (!granted) console.log(`      clipboard permission not granted: ${JSON.stringify(perm.error)}`);
  await cdp('Page.navigate', { url: `http://127.0.0.1:${PORT}/` });
  await sleep(500);
  const n0 = await settle();
  ok(n0 > 0, `a real browser draws the program: ${n0} document(s) on the canvas`);
  ok(await js('whisker.canvas.width') === 1024 && await js('whisker.canvas.height') === 600,
     'at the device size: a 1024x600 canvas');
  const ls = await js(`(()=>{const g=document.createElement('canvas').getContext('2d');
    g.font='100px monospace'; return (0.5-g.measureText('M').width/100).toFixed(4);})()`);
  console.log(`      the page set letter-spacing ${ls}em so a character is half its size wide`);
  await snap('web_first.png');
  ok(await js('whisker.images()') >= 1, 'the page fetched the logo once and inlined it into the SVG (§5r)');

  await type('Hello web');            // edit.c focuses the name field itself
  await click(100, 210);              // the city field: 48 + 8 + 32 + 6 + 48 + 6 + 32 + 6 = 186..234
  await type('Paris');
  await key('Enter', 'Enter');
  await settle();
  await sleep(300);
  ok(said.includes('enter  city = "Paris"'), 'a click moved focus to city, typing filled it, Enter printed it');
  ok(said.includes('focus  city'), 'the click arrived as a pointer event and focused the field');
  await snap('web_typed.png');

  // An overlay (§5l): Save opens a menu over the window; choose from it.
  // Save is under the Note memo, 380..444; the menu hangs below it.
  await click(100, 410);
  await settle();
  await sleep(200);
  ok(said.includes('click  save'), 'Save, clicked in the browser');
  await snap('web_menu.png');
  await click(100, 470);              // "Save now", the menu's first item, 450..494
  await settle();
  await sleep(300);
  ok(said.includes('click  save-now') && said.includes('close  the menu'),
     'and its menu: "Save now" chosen, the menu closed');

  // A check and a choice (§5m): Remember me is under Save, 450..486; the
  // list on the right keeps a choice.
  await click(40, 468);
  await click(700, 160);              // row 1 of the table: 56 + 40 + 4 = 100, + 40 + 2 = 142..182
  await settle();
  await sleep(300);
  ok(said.includes('change remember = 1'), 'the check ticked by a click: change remember = 1');
  ok(said.includes('select it1'), 'and a row of the tree chosen: select it1');
  // row 1 is the src folder's first file, so open docs: closed, the rows are
  // src (open) and its five, then docs at row 6 -- 100 + 6 x 42 = 352..392
  await click(700, 370);
  await settle();
  await sleep(300);
  ok(said.includes('open   docs'), 'a tap on a folder opens it -- the program owns expansion (§5p)');
  await snap('web_check.png');

  // §5s, with real keys and a real right-click. Name holds "Hello web": End,
  // then Shift-Home selects it all; Ctrl-C copies it, through the program,
  // to the system clipboard.
  await click(100, 118);              // name: 48 + 8 + 32 + 6 = 94..142
  await key('End', 'End');
  await key('Home', 'Home', 8);
  await settle();
  await snap('web_select.png');
  await key('c', 'KeyC', 2);
  await settle();
  await sleep(300);
  ok(said.includes('copy   name "Hello web"'), 'Shift-Home and Ctrl-C, real keys: the program copied "Hello web"');
  ok(await js('whisker.clip()') === 'Hello web', 'and the page was sent it');
  const how = await js('whisker.copied()');
  const sys = granted ? await js('navigator.clipboard.readText().catch(e=>"refused: "+e)') : '(no permission)';
  ok(how === 'clipboard' && sys === 'Hello web',
     `and put it on the system clipboard: ${how}, which reads back "${sys}"`);

  // A right-click past the end of city's "Paris": the menu, there; Paste.
  await rightClick(300, 210);
  await settle();
  await sleep(200);
  ok(said.includes('menu   city'), 'a real right-click: the browser\'s contextmenu, MENU on city');
  await snap('web_context.png');
  await click(320, 210 + 6 + 2 * 44 + 20);   // Paste: the third item, 44 apart
  await settle();
  await sleep(300);
  ok(said.includes('click  cx-paste') && said.includes('paste  city "ParisHello web"'),
     'Paste from the menu: the host\'s copy, at the caret the right-click put there');

  // And the browser's own paste: Ctrl-A, then Ctrl-V with the editor's
  // paste command, which reads the system clipboard and fires the page's
  // paste event.
  await key('a', 'KeyA', 2);
  await key('v', 'KeyV', 2, ['paste']);
  await settle();
  await sleep(400);
  ok(said.includes('paste  city "Hello web"'),
     'a real Ctrl-V: the browser\'s paste event, P to the host, Ctrl-V to the program, the selection replaced');

  // The banded rule: everything above was clipped repaints. Keep that canvas,
  // ask the program to draw it all again, and compare.
  await js('window.__a = whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data.slice(); true');
  const before = await settle();
  await js('whisker.send("X"); true');
  await sleep(400);
  const after = await settle();
  ok(after > before, 'asked to draw everything again, the program sent a full document');
  const diff = await js(`(()=>{const b=whisker.canvas.getContext("2d").getImageData(0,0,1024,600).data;
    let n=0; for(let i=0;i<b.length;i+=4) if(b[i]!==__a[i]||b[i+1]!==__a[i+1]||b[i+2]!==__a[i+2]) n++; return n;})()`);
  ok(diff === 0, `the canvas built from clipped repaints equals a full redraw, pixel for pixel (${diff} differ)`);
} finally {
  try { ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} browser check(s) failed` : '\nall browser checks passed');
process.exit(fails ? 1 : 0);
