// Renders the application icon scene to a PNG with headless Chromium.
//   node scripts/render-icon.js [size]   (default 1024)
// Writes packaging/icon/icon-1024.png (or icon-<size>.png). Needs Node.js and
// Playwright with Chromium (npm install -g playwright; npx playwright install
// chromium). Development tool only, not shipped.
const path = require('path');
const fs = require('fs');
const { chromium } = require('playwright');

(async () => {
  const size = parseInt(process.argv[2] || '1024', 10);
  const root = path.resolve(__dirname, '..');
  const frag = fs.readFileSync(path.join(root, 'packaging', 'icon', 'scene.frag'), 'utf8');
  const out = path.join(root, 'packaging', 'icon', `icon-${size}.png`);
  // Software WebGL, so the result does not depend on a GPU.
  const browser = await chromium.launch({ args: ['--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage();
  await page.setContent(`<canvas id="c" width="${size}" height="${size}"></canvas>`);
  const dataUrl = await page.evaluate(async ({ frag, size }) => {
    const gl = document.getElementById('c').getContext('webgl', { preserveDrawingBuffer: true, alpha: true });
    const compile = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    const prog = gl.createProgram();
    gl.attachShader(prog, compile(gl.VERTEX_SHADER, 'attribute vec2 p; void main(){ gl_Position = vec4(p, 0.0, 1.0); }'));
    gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, frag));
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
    gl.useProgram(prog);
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(prog, 'p');
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    gl.uniform2f(gl.getUniformLocation(prog, 'res'), size, size);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    // In tiles, so no single draw runs long enough to be cut off.
    gl.enable(gl.SCISSOR_TEST);
    for (let y = 0; y < size; y += 128) {
      for (let x = 0; x < size; x += 128) {
        gl.scissor(x, y, 128, 128);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        gl.finish();
        await new Promise(r => setTimeout(r, 0));
      }
    }
    return gl.canvas.toDataURL('image/png');
  }, { frag, size });
  fs.writeFileSync(out, Buffer.from(dataUrl.split(',')[1], 'base64'));
  await browser.close();
  console.log(`render-icon: wrote ${path.relative(root, out)}`);
})().catch(e => { console.error(`render-icon: ${e.message}`); process.exit(1); });
