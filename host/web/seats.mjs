// Several people at one program (design.md §5ab): floor.c through the web
// host, two Chrome pages at once -- two seats, two people on one floor.
//
//   node seats.mjs <whisker-floor-web binary> <dir for pictures>
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

// one page, driven over its own DevTools connection
class Page {
  constructor(target) { this.target = target; this.seq = 0; this.waiting = new Map(); }
  async open() {
    this.ws = new WebSocket(this.target.webSocketDebuggerUrl);
    await new Promise(r => this.ws.addEventListener('open', r));
    this.ws.addEventListener('message', e => {
      const m = JSON.parse(e.data);
      if (m.id && this.waiting.has(m.id)) { this.waiting.get(m.id)(m.result ?? m); this.waiting.delete(m.id); }
    });
    await this.cdp('Page.enable');
    await this.cdp('Runtime.enable');
  }
  cdp(method, params = {}) {
    const id = ++this.seq;
    this.ws.send(JSON.stringify({ id, method, params }));
    return new Promise(res => this.waiting.set(id, res));
  }
  async js(expr) {
    const r = await this.cdp('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
    return r.result?.value;
  }
  async go() { await this.cdp('Page.navigate', { url: `http://127.0.0.1:${PORT}/` }); await sleep(800); }
  async at(x, y) {
    return this.js(`(()=>{const r=whisker.canvas.getBoundingClientRect();
      return [r.left+(${x}+0.5)*r.width/whisker.canvas.width, r.top+(${y}+0.5)*r.height/whisker.canvas.height];})()`);
  }
  async click(x, y) {
    const [px, py] = await this.at(x, y);
    await this.cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: px, y: py, button: 'left', buttons: 1, clickCount: 1 });
    await this.cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: px, y: py, button: 'left', buttons: 0, clickCount: 1 });
  }
  async hover(x, y) {
    const [px, py] = await this.at(x, y);
    await this.cdp('Input.dispatchMouseEvent', { type: 'mouseMoved', x: px, y: py, button: 'none', buttons: 0 });
  }
  async right() {
    await this.cdp('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 'ArrowRight', code: 'ArrowRight', windowsVirtualKeyCode: 39 });
    await this.cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: 'ArrowRight', code: 'ArrowRight' });
  }
  // where a colour is on the canvas, below the status line: [lo, hi] in x
  async span(rgb) {
    return this.js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(0,36,1024,564).data;
      let lo=9999,hi=-1;for(let i=0;i<d.length;i+=4){if(Math.abs(d[i]-${rgb >> 16})<6&&Math.abs(d[i+1]-${(rgb >> 8) & 255})<6&&Math.abs(d[i+2]-${rgb & 255})<6){const x=(i/4)%1024;if(x<lo)lo=x;if(x>hi)hi=x;}}return [lo,hi];})()`);
  }
  async snap(name) {
    const url = await this.js('whisker.canvas.toDataURL("image/png")');
    writeFileSync(join(outDir, name), Buffer.from(url.split(',')[1], 'base64'));
    console.log(`      wrote ${join(outDir, name)}`);
  }
}

const GOLD = 0xd0a020;                 // Max
let a, b;
try {
  let first;
  for (let i = 0; i < 50 && !first; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${DEBUG}/json/list`)).json();
      first = list.find(t => t.type === 'page');
    } catch { await sleep(100); }
  }
  a = new Page(first);
  await a.open();
  await a.go();
  ok(/join   seat \d+ is Max/.test(said), 'the first page joins, and walks Max');
  const second = await (await fetch(`http://127.0.0.1:${DEBUG}/json/new?about:blank`, { method: 'PUT' })).json();
  b = new Page(second);
  await b.open();
  await b.go();
  const sa = await a.js('whisker.seat()'), sb = await b.js('whisker.seat()');
  ok(/join   seat \d+ is Ana/.test(said) && sa >= 0 && sb >= 0 && sa !== sb,
     `the second joins as a seat of its own (${sa}, ${sb}) and walks Ana`);
  await sleep(500);

  // each seat its own view: both start at the hall's door, 100,0; Right in
  // each pans that one an eighth of 1016, to 227 -- not the other to 354
  await a.right();
  await sleep(300);
  await b.right();
  await sleep(300);
  const views = said.match(/view   -?\d+,-?\d+/g) || [];
  ok(views.length === 2 && views[0] === 'view   227,0' && views[1] === 'view   227,0',
     `Right on each page pans that page's view alone: ${views.join(', ')}`);

  // Max as the second page sees him, before and after the first walks him
  const before = await b.span(GOLD);
  await a.click(600, 230);           // world 227 + 600 - 4, 230 - 36
  await sleep(300);
  ok(said.includes('walk   Max to 823,194') && !said.includes('walk   Ana'),
     'a click on the first page walks Max -- its own person, not Ana');
  await sleep(2500);
  const after = await b.span(GOLD);
  console.log(`      Max on the second page: x ${before[0]}..${before[1]}, then ${after[0]}..${after[1]}`);
  ok(before[0] >= 270 && before[0] <= 285 && after[0] >= 560 && after[0] <= 575,
     'and the second page sees Max walk: from 277 to 565 on its own screen');
  await b.snap('seats_second.png');

  // hover is each seat's: the second points at Max, and says so
  await b.hover(after[0] + 10, 179 + 36 + 10);
  await sleep(400);
  ok(said.includes('hover  Max'), 'the second page points at Max: its status line names him');

  await b.click(300, 450);           // world 227 + 300 - 4, 450 - 36
  await sleep(300);
  ok(said.includes('walk   Ana to 523,414') && (said.match(/walk   Max/g) || []).length === 1,
     'a click on the second walks Ana, and Max\'s walk is untouched');

  // a page that goes gives its person back; one that comes takes them again
  await fetch(`http://127.0.0.1:${DEBUG}/json/close/${second.id}`);
  await sleep(1000);
  ok(/gone   seat \d+, Ana walks on/.test(said), 'the second page is closed: Ana walks on by herself');
  const third = await (await fetch(`http://127.0.0.1:${DEBUG}/json/new?about:blank`, { method: 'PUT' })).json();
  b = new Page(third);
  await b.open();
  await b.go();
  ok((said.match(/join   seat \d+ is Ana/g) || []).length === 2, 'and a page that comes is Ana again -- the first free');
} finally {
  if (fails) console.log("---- said:\n" + said.slice(-1500));
  try { a?.ws?.close(); b?.ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} seat check(s) failed` : '\nall seat checks passed');
process.exit(fails ? 1 : 0);
