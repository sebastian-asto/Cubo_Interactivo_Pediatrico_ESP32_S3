import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// Exercise the actual editor's format functions without a browser or dependencies.
const pageUrl = new URL('../../documents/index.html', import.meta.url);
const html = fs.readFileSync(pageUrl, 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script); // Syntax-check the complete editor, including event handlers.
const context = vm.createContext({ MAX_WIDTH: 240, MAX_HEIGHT: 240 });
function loadFunction(name, nextName) {
  const start = script.indexOf(`      function ${name}(`);
  const end = script.indexOf(`      function ${nextName}(`, start);
  assert.ok(start >= 0 && end > start, `Function ${name} must exist`);
  vm.runInContext(script.slice(start, end), context);
}
loadFunction('rgbTo565', 'rgb565ToColor');
loadFunction('rgb565ToColor', 'format565');
loadFunction('sanitizeName', 'showToast');
loadFunction('createTft', 'exportTft');
loadFunction('parseTft', 'downloadBlob');
loadFunction('parseTxt', 'importTxt');

context.state = { width: 2, height: 2, pixels: [0xff0000, 0x00ff00, 0x0000ff, 0xffffff] };
const image = context.createTft();
assert.deepEqual(Array.from(new Uint8Array(image)), [
  0x54, 0x46, 0x54, 0x31, 0, 2, 0, 2,
  0xf8, 0, 0x07, 0xe0, 0, 0x1f, 0xff, 0xff,
]);
const imported = context.parseTft(image);
assert.equal(imported.width, 2);
assert.equal(imported.height, 2);
assert.deepEqual(Array.from(imported.pixels), context.state.pixels);

function altered(change) {
  const copy = image.slice(0);
  change(new Uint8Array(copy));
  return copy;
}
assert.throws(() => context.parseTft(image.slice(0, 7)), /firma/);
assert.throws(() => context.parseTft(altered(bytes => { bytes[0] = 0; })), /firma/);
assert.throws(() => context.parseTft(altered(bytes => { bytes[5] = 0; })), /Dimensiones/);
assert.throws(() => context.parseTft(altered(bytes => { bytes[5] = 241; })), /Dimensiones/);
assert.throws(() => context.parseTft(image.slice(0, -1)), /Dimensiones/);
const extra = new Uint8Array(image.byteLength + 1);
extra.set(new Uint8Array(image));
assert.throws(() => context.parseTft(extra.buffer), /Dimensiones/);

const txtHeader = '# TFT_PIXEL_ART_V1\nname=test\nwidth=2\nheight=2\npixel_format=RGB565\n';
for (const [encoding, data] of [
  ['RAW_HEX', '0xF800,0x07E0,0x001F,0xFFFF'],
  ['RLE', '1:0xF800,1:0x07E0,1:0x001F,1:0xFFFF'],
]) {
  const txtImage = context.parseTxt(`${txtHeader}encoding=${encoding}\nlayout=ROW_MAJOR\ndata:\n${data}\n`);
  assert.deepEqual(Array.from(txtImage.pixels), context.state.pixels);
}

// Full frame: known quadrant colors catch row order and both edge coordinates.
context.state = {
  width: 240, height: 240,
  pixels: Array.from({ length: 240 * 240 }, (_, i) =>
    i < 240 * 120 ? (i % 240 < 120 ? 0xff0000 : 0x00ff00) :
      (i % 240 < 120 ? 0x0000ff : 0xffffff)),
};
const fullImage = context.createTft();
assert.equal(fullImage.byteLength, 115208);
assert.deepEqual(Array.from(context.parseTft(fullImage).pixels), context.state.pixels);
assert.match(html, /id="esp32Button"/);
assert.match(script, /getElementById\("esp32Button"\)\.addEventListener\("click", exportTft\)/);
if (process.argv[2]) {
  fs.writeFileSync(process.argv[2], Buffer.from(fullImage));
}
console.log('OK: JS syntax, RGB565 byte order, TFT1 round-trip, RAW/RLE imports, invalid files and 240x240 frame.');

const emotionDir = new URL('../main/assets/emotions/', import.meta.url);
if (fs.existsSync(emotionDir)) {
  const names = fs.readdirSync(emotionDir).filter(name => name.endsWith('.tft'));
  assert.equal(names.length, 16);
  const registry = fs.readFileSync(new URL('../main/app/emotions/emotions.c', import.meta.url), 'utf8');
  const unique = new Set();
  for (const name of names) {
    assert.match(name, /^[a-z]{1,8}\.tft$/, 'FAT 8.3 filename required');
    const file = fs.readFileSync(new URL(name, emotionDir));
    const buffer = file.buffer.slice(file.byteOffset, file.byteOffset + file.byteLength);
    const decoded = context.parseTft(buffer);
    assert.equal(decoded.width, 240);
    assert.equal(decoded.height, 240);
    context.state = decoded;
    assert.deepEqual(Buffer.from(context.createTft()), file, `Editor round-trip: ${name}`);
    assert.ok(registry.includes(`X(${name.slice(0, -4)},`), `Firmware registry: ${name}`);
    unique.add(file.toString('base64'));
  }
  assert.equal(unique.size, 16, 'Every emotion must have different pixels');
  console.log('OK: 16 distinct emotions, FAT 8.3 names, registry and exact editor import/export round-trips.');
}
