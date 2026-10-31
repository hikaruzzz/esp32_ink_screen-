/*
 * 在 Node 里跑网页 index.html 里的量化脚本，输出 2bpp 帧。
 * 由 tools/fb_crosscheck.py 调用，用来确认「手机预览 = 上传字节」。
 *
 * 用法：node tools/fb_crosscheck.js <src.rgb> <out.2bpp>
 *   src.rgb：768*552*3 字节的 sRGB（已 cover 裁切到面板尺寸）
 */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const [srcPath, outPath] = process.argv.slice(2);
if (!srcPath || !outPath) {
  console.error('用法: node fb_crosscheck.js <src.rgb> <out.2bpp>');
  process.exit(2);
}

const W = 768, H = 552;
const raw = fs.readFileSync(srcPath);
if (raw.length !== W * H * 3) {
  console.error(`输入长度 ${raw.length}，应为 ${W * H * 3}`);
  process.exit(2);
}

/* 把 sRGB 原始像素包成 canvas 的 RGBA 缓冲 */
const rgba = new Uint8ClampedArray(W * H * 4);
for (let i = 0, j = 0; i < W * H; i++, j += 3) {
  rgba[i * 4] = raw[j];
  rgba[i * 4 + 1] = raw[j + 1];
  rgba[i * 4 + 2] = raw[j + 2];
  rgba[i * 4 + 3] = 255;
}

const imageData = { data: rgba, width: W, height: H };
const canvasCtx = {
  fillStyle: '#fff',
  fillRect() {},
  drawImage() {},
  getImageData() { return imageData; },
  putImageData() {},
};

/* 同一个 id 必须返回同一个对象，否则脚本里设置滑块值会被丢掉 */
const els = new Map();
function fakeEl(id) {
  if (!els.has(id)) {
    els.set(id, {
      id,
      value: '1',
      style: {},
      textContent: '',
      className: '',
      disabled: false,
      addEventListener() {},
      getContext() { return canvasCtx; },
    });
  }
  return els.get(id);
}

let bitmapSet = null;
const sandbox = {
  console,
  performance,
  setTimeout,
  clearTimeout,
  setInterval: () => 0,
  clearInterval: () => {},
  document: {
    getElementById: fakeEl,
    createElement: () => fakeEl('canvas'),
  },
  localStorage: { getItem: () => null, setItem() {} },
  fetch: () => new Promise(() => {}),   // /status 等请求挂起即可
  Uint8Array, Uint8ClampedArray, Float32Array, Math,
};

vm.createContext(sandbox);

/* 取出页面里唯一的内联脚本 */
const html = fs.readFileSync(path.join(__dirname, 'epd_photo/main/web/index.html'), 'utf8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) {
  console.error('没找到 <script> 段');
  process.exit(2);
}

vm.runInContext(m[1], sandbox, { filename: 'index.html:script' });

/* 画质滑块用默认值，和 Python 参考实现保持一致 */
vm.runInContext(`
  document.getElementById('qexpo').value = 1.35;
  document.getElementById('qgamma').value = 1.10;
  document.getElementById('qcon').value = 1.05;
  document.getElementById('qsat').value = 1.25;
  bitmap = { width: 768, height: 552 };
`, sandbox);

const secs = vm.runInContext('renderFinal()', sandbox);
const packed = vm.runInContext('packedFrame', sandbox);
if (!packed) {
  console.error('packedFrame 为空');
  process.exit(1);
}
fs.writeFileSync(outPath, Buffer.from(packed));
console.log(`js: ${packed.length} 字节, ${secs.toFixed(2)} s`);
