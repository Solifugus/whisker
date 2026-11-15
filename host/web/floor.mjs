// A conference floor in a real browser (design.md §5q step 3): floor.c
// through the web host, driven as a person would -- meet someone, walk,
// zoom with the wheel, look around with a drag.
//
//   node floor.mjs <whisker-floor-web binary> <dir for pictures>
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [bin, outDir] = process.argv.slice(2);
const PORT = 18093, DEBUG = 19335;
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
  ok((await settle()) > 0, 'the conference floor draws in a real browser');
  await sleep(1500);
  await snap('floor_first.png');

  // "You" start at world 500,300; the view at 100,0, the floor from 4,36
  // (the window's padding, then a 32-px status line), puts them at
  // 404..474, 336..366
  await click(435, 351);
  await sleep(400);
  ok(said.includes('meet   You waves.'), 'a click on a person meets them: "You waves."');

  // §5q step 2b: a poster is a stack -- world 120,20, 300 x 116, so on the
  // screen 24..324, 56..172; its Visit button 30..318, 140..166
  await click(174, 153);
  await sleep(400);
  ok(said.includes('poster You visit "Tiny GUIs on RISC-V".'),
     'a poster\'s own button: hit descends into the poster, and the program hears Visit');
  await click(120, 98);
  await sleep(400);
  ok(said.includes('poster Tiny GUIs on RISC-V: a GUI in 3.8 KB on a C5'),
     'and its abstract, a word of wrapped text inside it, is the poster\'s too');

  // bare floor between the posters (to 172) and where anyone walks (from 284)
  await click(600, 230);
  await sleep(400);
  ok(said.includes('walk   to 696,194'), 'a click on bare floor walks you there: world 696,194 -- 100 + 600 - 4, 230 - 36');
  await sleep(2500);
  await snap('floor_walked.png');

  // §5q step 4: now walk toward the floor just under a poster -- world
  // 696,150, so you would stand at 135..165, and "Damage, banded" ends at
  // 136: you stop against it, and the program says so
  await click(600, 186);
  await sleep(1500);
  ok(said.includes('walk   to 696,150') && said.includes('bump   You reach "Damage, banded".'),
     'walking into a poster: whisker says what the step would touch, the program stops you there');

  // §5q step 5: walk toward the right edge -- world 1096,364. Unfollowed,
  // you would end at screen x 965..1035, off the edge; followed with a 30%
  // margin, the view glides so you stay within 1016 - 305 - 70 + 4 = 645
  await click(1000, 400);
  await sleep(3000);
  const gold = await js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(0,36,1024,564).data;
    let lo=9999,hi=-1;for(let i=0;i<d.length;i+=4){if(Math.abs(d[i]-0xd0)<6&&Math.abs(d[i+1]-0xa0)<6&&Math.abs(d[i+2]-0x20)<6){const x=(i/4)%1024;if(x<lo)lo=x;if(x>hi)hi=x;}}return [lo,hi];})()`);
  console.log(`      you, in gold, from x ${gold[0]} to ${gold[1]}`);
  ok(said.includes('walk   to 1096,364') && gold[1] > 0 && gold[1] <= 720,
     'the view follows you: walked toward the right edge, you are kept 30% in from it');
  await snap('floor_follow.png');

  // the wheel zooms about the pointer
  {
    const [px, py] = await at(500, 300);
    await cdp('Input.dispatchMouseEvent', { type: 'mouseWheel', x: px, y: py, deltaX: 0, deltaY: -100 });
    await sleep(500);
    const m = said.match(/view   (-?\d+),(-?\d+) at (\d+)\/256/g) || [];
    const last = m.length ? m[m.length - 1].match(/at (\d+)/)[1] : '0';
    ok(m.length > 0 && Number(last) > 256, `the wheel zooms in: ${m.length ? m[m.length - 1] : 'no view notice'}`);
  }
  // and a drag looks around
  {
    const before = (said.match(/view   /g) || []).length;
    const [ax, ay] = await at(900, 260), [bx] = await at(700, 260);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', x: ax, y: ay, button: 'left', buttons: 1, clickCount: 1 });
    for (let k = 1; k <= 5; k++)
      await cdp('Input.dispatchMouseEvent', { type: 'mouseMoved', x: ax + (bx - ax) * k / 5, y: ay, button: 'left', buttons: 1 });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', x: bx, y: ay, button: 'left', buttons: 0, clickCount: 1 });
    await sleep(500);
    ok((said.match(/view   /g) || []).length > before && !said.slice(said.length - 200).includes('walk   to 8'),
       'a drag pans the floor -- VIEW notices -- and is not a walk');
    ok(said.includes('follow off'), 'and looking around stops the view following you');
  }
  // §5z: the floor has the keyboard from the start -- the arrows look around
  {
    const before = (said.match(/view   /g) || []).length;
    await key('ArrowRight', 'ArrowRight');
    await key('ArrowDown', 'ArrowDown');
    await sleep(400);
    ok((said.match(/view   /g) || []).length === before + 2, 'the arrow keys look around: two VIEW notices for Right and Down');
    await type('-');
    await sleep(300);
    const m = said.match(/view   -?\d+,-?\d+ at (\d+)\/256\n$/);
    ok(m && Number(m[1]) < 356, `and - zooms out: ${m ? m[0].trim() : 'nothing'}`);
  }
  await snap('floor_zoomed.png');

  // zoomed right out the whole floor is in view, and the document is over
  // the 4 KB a window on /w0 takes: the program draws it in parts. Every
  // person's colour on the canvas says both parts arrived.
  for (let k = 0; k < 12; k++) { await type('-'); await sleep(60); }
  await sleep(600);
  {
    const fills = [0xd0a020, 0x3a7ab0, 0x8a4a9a, 0x3a9a6a, 0xb05a3a, 0x5a6ab0, 0x9a8a3a, 0x4a8a8a, 0xa04a6a, 0x6a9a3a];
    const seen = await js(`(()=>{const d=whisker.canvas.getContext("2d").getImageData(0,36,1024,564).data;
      const f=${JSON.stringify(fills)};const n=f.map(()=>0);
      for(let i=0;i<d.length;i+=4)for(let j=0;j<f.length;j++)if(d[i]===(f[j]>>16)&&d[i+1]===((f[j]>>8)&255)&&d[i+2]===(f[j]&255))n[j]++;return n;})()`);
    console.log(`      pixels of each person's colour: ${seen.join(' ')}`);
    ok(said.includes('bands  a frame over 4 KB goes in parts') && seen.every(v => v > 50),
       'zoomed out to the whole floor: over 4 KB, drawn in parts -- and all ten people are on the canvas');
  }
  await snap('floor_all.png');

} finally {
  try { ws?.close(); } catch {}
  chrome.kill();
  app.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `\n${fails} floor check(s) failed` : '\nall floor checks passed');
process.exit(fails ? 1 : 0);
