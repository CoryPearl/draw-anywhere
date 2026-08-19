const SIZE = 64;
const canvas = document.getElementById('matrix');
const ctx = canvas.getContext('2d', { willReadFrequently: true });
const colorPicker = document.getElementById('colorPicker');
const hexColor = document.getElementById('hexColor');
const brushSize = document.getElementById('brushSize');
const brightness = document.getElementById('brightness');
const brightnessValue = document.getElementById('brightnessValue');
const toolLabel = document.getElementById('toolLabel');
const statusEl = document.getElementById('status');
const imageUpload = document.getElementById('imageUpload');
const imageDropZone = document.getElementById('imageDropZone');
const imageFileName = document.getElementById('imageFileName');
const settingsDialog = document.getElementById('settingsDialog');
const espUrl = document.getElementById('espUrl');
const themeToggle = document.getElementById('themeToggle');
const textInput = document.getElementById('textInput');
const textSize = document.getElementById('textSize');
const undoBtn = document.getElementById('undoBtn');
const redoBtn = document.getElementById('redoBtn');
const imageSize = document.getElementById('imageSize');
const placeImageBtn = document.getElementById('placeImageBtn');
const cancelImageBtn = document.getElementById('cancelImageBtn');
const imagePopup = document.getElementById('imagePopup');

let pixels = Array.from({ length: SIZE * SIZE }, () => '#000000');
let tool = 'brush';
let drawing = false;
let startPoint = null;
let lastPaintPoint = null;
let previewPixels = null;
let undoStack = [];
let redoStack = [];
let imageLayer = null;
let imageDragOffset = null;

const PIXEL_FONT = {
  A: ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
  B: ['11110', '10001', '10001', '11110', '10001', '10001', '11110'],
  C: ['01111', '10000', '10000', '10000', '10000', '10000', '01111'],
  D: ['11110', '10001', '10001', '10001', '10001', '10001', '11110'],
  E: ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
  F: ['11111', '10000', '10000', '11110', '10000', '10000', '10000'],
  G: ['01111', '10000', '10000', '10011', '10001', '10001', '01111'],
  H: ['10001', '10001', '10001', '11111', '10001', '10001', '10001'],
  I: ['11111', '00100', '00100', '00100', '00100', '00100', '11111'],
  J: ['00111', '00010', '00010', '00010', '10010', '10010', '01100'],
  K: ['10001', '10010', '10100', '11000', '10100', '10010', '10001'],
  L: ['10000', '10000', '10000', '10000', '10000', '10000', '11111'],
  M: ['10001', '11011', '10101', '10101', '10001', '10001', '10001'],
  N: ['10001', '11001', '10101', '10011', '10001', '10001', '10001'],
  O: ['01110', '10001', '10001', '10001', '10001', '10001', '01110'],
  P: ['11110', '10001', '10001', '11110', '10000', '10000', '10000'],
  Q: ['01110', '10001', '10001', '10001', '10101', '10010', '01101'],
  R: ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
  S: ['01111', '10000', '10000', '01110', '00001', '00001', '11110'],
  T: ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
  U: ['10001', '10001', '10001', '10001', '10001', '10001', '01110'],
  V: ['10001', '10001', '10001', '10001', '10001', '01010', '00100'],
  W: ['10001', '10001', '10001', '10101', '10101', '10101', '01010'],
  X: ['10001', '10001', '01010', '00100', '01010', '10001', '10001'],
  Y: ['10001', '10001', '01010', '00100', '00100', '00100', '00100'],
  Z: ['11111', '00001', '00010', '00100', '01000', '10000', '11111'],
  0: ['01110', '10001', '10011', '10101', '11001', '10001', '01110'],
  1: ['00100', '01100', '00100', '00100', '00100', '00100', '01110'],
  2: ['01110', '10001', '00001', '00010', '00100', '01000', '11111'],
  3: ['11110', '00001', '00001', '01110', '00001', '00001', '11110'],
  4: ['10010', '10010', '10010', '11111', '00010', '00010', '00010'],
  5: ['11111', '10000', '10000', '11110', '00001', '00001', '11110'],
  6: ['01110', '10000', '10000', '11110', '10001', '10001', '01110'],
  7: ['11111', '00001', '00010', '00100', '01000', '01000', '01000'],
  8: ['01110', '10001', '10001', '01110', '10001', '10001', '01110'],
  9: ['01110', '10001', '10001', '01111', '00001', '00001', '01110'],
  '.': ['00000', '00000', '00000', '00000', '00000', '01100', '01100'],
  '!': ['00100', '00100', '00100', '00100', '00100', '00000', '00100'],
  '?': ['01110', '10001', '00001', '00010', '00100', '00000', '00100'],
  '-': ['00000', '00000', '00000', '11111', '00000', '00000', '00000'],
  _: ['00000', '00000', '00000', '00000', '00000', '00000', '11111'],
  ':': ['00000', '01100', '01100', '00000', '01100', '01100', '00000'],
  '/': ['00001', '00010', '00010', '00100', '01000', '01000', '10000'],
  ' ': ['00000', '00000', '00000', '00000', '00000', '00000', '00000'],
  ',': ['00000', '00000', '00000', '00000', '00100', '00100', '01000'],
};

const savedTheme = localStorage.getItem('matrix-theme') || 'light';
document.documentElement.classList.toggle('dark', savedTheme === 'dark');
themeToggle.checked = savedTheme === 'dark';
const DEFAULT_BACKEND_URL =
  'https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json';
espUrl.value = normalizeBackendUrl(
  localStorage.getItem('matrix-backend-url') ||
    localStorage.getItem('matrix-esp-url') ||
    DEFAULT_BACKEND_URL,
);
localStorage.setItem('matrix-backend-url', espUrl.value);
const savedBrightness = localStorage.getItem('matrix-brightness');
if (savedBrightness) brightness.value = savedBrightness;
brightnessValue.textContent = brightness.value;

function normalizeBackendUrl(value) {
  const trimmed = String(value || '')
    .trim()
    .replace('cory-pearl.gt.lc', 'cory-pearl.gt.tc');
  if (!trimmed) return '';
  if (/cory-pearl\.gt\.tc\/matrix\/api\.php/i.test(trimmed)) {
    return DEFAULT_BACKEND_URL;
  }
  if (
    location.protocol === 'https:' &&
    location.hostname === 'cory-pearl.gt.tc' &&
    /^https?:\/\/cory-pearl\.gt\.tc\//i.test(trimmed)
  ) {
    const parsed = new URL(trimmed);
    return `${location.origin}${parsed.pathname}${parsed.search}`;
  }
  if (trimmed.startsWith('http://')) {
    return trimmed.replace('http://', 'https://');
  }
  if (!/^https?:\/\//i.test(trimmed)) {
    return `https://${trimmed}`;
  }
  return trimmed;
}

function matrixPayload() {
  return {
    ok: true,
    hasFrame: true,
    width: SIZE,
    height: SIZE,
    frameHex: frameHexString(),
    brightness: Math.max(1, Math.min(100, Number(brightness.value) || 100)),
    sequence: Date.now() % 2147483647,
    updatedAt: new Date().toISOString(),
  };
}

function normalizeHex(value) {
  const match = String(value)
    .trim()
    .match(/^#?([0-9a-f]{6})$/i);
  return match ? `#${match[1].toLowerCase()}` : null;
}

function frameHexString() {
  return imageLayerPixels()
    .map((hex) => hex.slice(1))
    .join('');
}

function applyIncomingFrame(data) {
  if (!data || data.hasFrame === false) return false;
  let applied = false;
  if (typeof data.frameHex === 'string' && data.frameHex.length >= SIZE * SIZE * 6) {
    const next = [];
    for (let i = 0; i < SIZE * SIZE; i += 1) {
      const chunk = data.frameHex.slice(i * 6, i * 6 + 6);
      if (!/^[0-9a-f]{6}$/i.test(chunk)) break;
      next.push(`#${chunk.toLowerCase()}`);
    }
    if (next.length === SIZE * SIZE) {
      pixels = next;
      applied = true;
    }
  }
  if (typeof data.brightness === 'number' && data.brightness >= 1 && data.brightness <= 100) {
    brightness.value = data.brightness;
    brightnessValue.textContent = brightness.value;
    localStorage.setItem('matrix-brightness', brightness.value);
  }
  return applied;
}

async function loadLastFrame() {
  const url = normalizeBackendUrl(espUrl.value);
  if (!url) return;
  try {
    setStatus('Loading last frame...');
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 12000);
    const response = await fetch(url, {
      signal: controller.signal,
      headers: { Accept: 'application/json,text/plain,*/*' },
    });
    clearTimeout(timeout);
    if (!response.ok) {
      setStatus(`Could not load last frame (${response.status})`);
      return;
    }
    const data = await response.json();
    const applied = applyIncomingFrame(data);
    imageLayer = null;
    undoStack = [];
    redoStack = [];
    updateHistoryButtons();
    render();
    setStatus(applied ? 'Loaded last frame from backend' : 'No saved frame yet');
  } catch (error) {
    setStatus(
      error.name === 'AbortError'
        ? 'Load timed out. Check backend URL.'
        : 'Could not load last frame',
    );
  }
}

function setStatus(message) {
  statusEl.textContent = message;
}

function updateHistoryButtons() {
  undoBtn.disabled = undoStack.length === 0;
  redoBtn.disabled = redoStack.length === 0;
}

function snapshotPixels() {
  return pixels.slice();
}

function recordHistory() {
  undoStack.push(snapshotPixels());
  if (undoStack.length > 100) undoStack.shift();
  redoStack = [];
  updateHistoryButtons();
}

function index(x, y) {
  return y * SIZE + x;
}

function inBounds(x, y) {
  return x >= 0 && x < SIZE && y >= 0 && y < SIZE;
}

function setPixel(x, y, color) {
  if (inBounds(x, y)) pixels[index(x, y)] = color;
}

function paintBrush(x, y, color) {
  const radius = Math.max(1, Number(brushSize.value));
  const half = Math.floor(radius / 2);
  for (let yy = y - half; yy < y - half + radius; yy += 1) {
    for (let xx = x - half; xx < x - half + radius; xx += 1) {
      setPixel(xx, yy, color);
    }
  }
}

function paintBrushStroke(from, to, color) {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const steps = Math.max(Math.abs(dx), Math.abs(dy), 1);
  for (let i = 0; i <= steps; i += 1) {
    const x = Math.round(from.x + (dx * i) / steps);
    const y = Math.round(from.y + (dy * i) / steps);
    paintBrush(x, y, color);
  }
}

function render(sourcePixels = pixels) {
  const image = ctx.createImageData(SIZE, SIZE);
  sourcePixels.forEach((hex, i) => {
    image.data[i * 4] = parseInt(hex.slice(1, 3), 16);
    image.data[i * 4 + 1] = parseInt(hex.slice(3, 5), 16);
    image.data[i * 4 + 2] = parseInt(hex.slice(5, 7), 16);
    image.data[i * 4 + 3] = 255;
  });
  ctx.putImageData(image, 0, 0);
}

function imageLayerPixels() {
  if (!imageLayer) return pixels;
  const next = pixels.slice();
  const width = Math.max(1, imageLayer.width);
  const height = Math.max(1, imageLayer.height);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const tx = imageLayer.x + x;
      const ty = imageLayer.y + y;
      if (!inBounds(tx, ty)) continue;
      const sx = Math.min(SIZE - 1, Math.floor((x / width) * SIZE));
      const sy = Math.min(SIZE - 1, Math.floor((y / height) * SIZE));
      const color = imageLayer.source[index(sx, sy)];
      if (color !== '#000000') next[index(tx, ty)] = color;
    }
  }
  return next;
}

function renderImageLayer() {
  render(imageLayerPixels());
}

function updateImageControls() {
  const active = Boolean(imageLayer);
  imagePopup.hidden = !active;
  imageSize.disabled = !active;
  placeImageBtn.disabled = !active;
  cancelImageBtn.disabled = !active;
}

function setImageSize(nextSize) {
  if (!imageLayer) return;
  const size = Math.max(4, Math.min(SIZE, Number(nextSize) || SIZE));
  const centerX = imageLayer.x + imageLayer.width / 2;
  const centerY = imageLayer.y + imageLayer.height / 2;
  imageLayer.width = size;
  imageLayer.height = size;
  imageLayer.x = Math.round(centerX - size / 2);
  imageLayer.y = Math.round(centerY - size / 2);
  renderImageLayer();
}

function pointInImageLayer(point) {
  return (
    imageLayer &&
    point.x >= imageLayer.x &&
    point.x < imageLayer.x + imageLayer.width &&
    point.y >= imageLayer.y &&
    point.y < imageLayer.y + imageLayer.height
  );
}

function renderPreview(from, to) {
  if (!['line', 'rect', 'circle'].includes(tool)) return;
  previewPixels = pixels.slice();
  const color = colorPicker.value;
  if (tool === 'line') drawLine(from, to, color, previewPixels);
  if (tool === 'rect') drawRect(from, to, color, previewPixels);
  if (tool === 'circle') drawCircle(from, to, color, previewPixels);
  render(previewPixels);
}

function pointFromEvent(event) {
  const rect = canvas.getBoundingClientRect();
  return {
    x: Math.max(
      0,
      Math.min(
        SIZE - 1,
        Math.floor(((event.clientX - rect.left) / rect.width) * SIZE),
      ),
    ),
    y: Math.max(
      0,
      Math.min(
        SIZE - 1,
        Math.floor(((event.clientY - rect.top) / rect.height) * SIZE),
      ),
    ),
  };
}

function writePixel(targetPixels, x, y, color) {
  if (inBounds(x, y)) targetPixels[index(x, y)] = color;
}

function drawLine(a, b, color, targetPixels = pixels) {
  let x0 = a.x;
  let y0 = a.y;
  const x1 = b.x;
  const y1 = b.y;
  const dx = Math.abs(x1 - x0);
  const sx = x0 < x1 ? 1 : -1;
  const dy = -Math.abs(y1 - y0);
  const sy = y0 < y1 ? 1 : -1;
  let err = dx + dy;
  while (true) {
    writePixel(targetPixels, x0, y0, color);
    if (x0 === x1 && y0 === y1) break;
    const e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

function drawRect(a, b, color, targetPixels = pixels) {
  const minX = Math.min(a.x, b.x);
  const maxX = Math.max(a.x, b.x);
  const minY = Math.min(a.y, b.y);
  const maxY = Math.max(a.y, b.y);
  for (let x = minX; x <= maxX; x += 1) {
    writePixel(targetPixels, x, minY, color);
    writePixel(targetPixels, x, maxY, color);
  }
  for (let y = minY; y <= maxY; y += 1) {
    writePixel(targetPixels, minX, y, color);
    writePixel(targetPixels, maxX, y, color);
  }
}

function drawCircle(a, b, color, targetPixels = pixels) {
  const minX = Math.min(a.x, b.x);
  const maxX = Math.max(a.x, b.x);
  const minY = Math.min(a.y, b.y);
  const maxY = Math.max(a.y, b.y);
  const rx = Math.max(1, (maxX - minX) / 2);
  const ry = Math.max(1, (maxY - minY) / 2);
  const cx = (minX + maxX) / 2;
  const cy = (minY + maxY) / 2;
  const steps = Math.max(16, Math.ceil(2 * Math.PI * Math.max(rx, ry) * 1.4));
  for (let i = 0; i < steps; i += 1) {
    const angle = (i / steps) * Math.PI * 2;
    const x = Math.round(cx + Math.cos(angle) * rx);
    const y = Math.round(cy + Math.sin(angle) * ry);
    writePixel(targetPixels, x, y, color);
  }
}

function floodFill(x, y, color) {
  const target = pixels[index(x, y)];
  if (target === color) return;
  const queue = [[x, y]];
  while (queue.length) {
    const [cx, cy] = queue.pop();
    if (!inBounds(cx, cy) || pixels[index(cx, cy)] !== target) continue;
    setPixel(cx, cy, color);
    queue.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
  }
}

function drawTextAt(point) {
  const color = colorPicker.value;
  const scale = Math.max(1, Math.min(4, Number(textSize.value) || 1));
  const lines = (textInput.value || 'Text').toUpperCase().split(/\r?\n/);
  lines.forEach((line, lineIndex) => {
    let cursorX = point.x;
    const cursorY = point.y + lineIndex * 8 * scale;
    line.split('').forEach((char) => {
      if (char === ' ') {
        cursorX += 3 * scale;
        return;
      }
      const glyph = PIXEL_FONT[char] || PIXEL_FONT['?'];
      glyph.forEach((row, gy) => {
        row.split('').forEach((cell, gx) => {
          if (cell !== '1') return;
          for (let sy = 0; sy < scale; sy += 1) {
            for (let sx = 0; sx < scale; sx += 1) {
              setPixel(
                cursorX + gx * scale + sx,
                cursorY + gy * scale + sy,
                color,
              );
            }
          }
        });
      });
      cursorX += 6 * scale;
    });
  });
}

function applyTool(point, finalPoint = null) {
  const color = tool === 'eraser' ? '#000000' : colorPicker.value;
  if (tool === 'brush' || tool === 'eraser')
    paintBrush(point.x, point.y, color);
  if (tool === 'fill') floodFill(point.x, point.y, color);
  if (tool === 'text') drawTextAt(point);
  if (finalPoint && tool === 'line') drawLine(point, finalPoint, color);
  if (finalPoint && tool === 'rect') drawRect(point, finalPoint, color);
  if (finalPoint && tool === 'circle') drawCircle(point, finalPoint, color);
  render();
}

canvas.addEventListener('pointerdown', (event) => {
  canvas.setPointerCapture(event.pointerId);
  startPoint = pointFromEvent(event);
  if (pointInImageLayer(startPoint)) {
    imageDragOffset = {
      x: startPoint.x - imageLayer.x,
      y: startPoint.y - imageLayer.y,
    };
    drawing = true;
    setStatus('Move image');
    return;
  }
  drawing = true;
  lastPaintPoint = startPoint;
  recordHistory();
  if (['brush', 'eraser', 'fill', 'text'].includes(tool)) {
    applyTool(startPoint);
  }
});

canvas.addEventListener('pointermove', (event) => {
  if (!drawing) return;
  const point = pointFromEvent(event);
  if (imageLayer && imageDragOffset) {
    imageLayer.x = point.x - imageDragOffset.x;
    imageLayer.y = point.y - imageDragOffset.y;
    renderImageLayer();
    return;
  }
  if (['brush', 'eraser'].includes(tool)) {
    const color = tool === 'eraser' ? '#000000' : colorPicker.value;
    paintBrushStroke(lastPaintPoint || point, point, color);
    lastPaintPoint = point;
    render();
  } else if (['line', 'rect', 'circle'].includes(tool) && startPoint) {
    renderPreview(startPoint, point);
  }
});

canvas.addEventListener('pointerup', (event) => {
  if (!drawing) return;
  const endPoint = pointFromEvent(event);
  if (['line', 'rect', 'circle'].includes(tool))
    applyTool(startPoint, endPoint);
  previewPixels = null;
  drawing = false;
  startPoint = null;
  lastPaintPoint = null;
  imageDragOffset = null;
});

document.getElementById('tools').addEventListener('click', (event) => {
  const button = event.target.closest('[data-tool]');
  if (!button) return;
  tool = button.dataset.tool;
  document
    .querySelectorAll('.tool')
    .forEach((item) => item.classList.toggle('active', item === button));
  toolLabel.textContent = button.textContent;
  setStatus(`${button.textContent} selected`);
});

colorPicker.addEventListener('input', () => {
  hexColor.value = colorPicker.value;
});

hexColor.addEventListener('change', () => {
  const next = normalizeHex(hexColor.value);
  if (next) {
    colorPicker.value = next;
    hexColor.value = next;
  } else {
    hexColor.value = colorPicker.value;
  }
});

brightness.addEventListener('input', () => {
  brightnessValue.textContent = brightness.value;
  localStorage.setItem('matrix-brightness', brightness.value);
});

function importImageFile(file) {
  if (!file) return;
  if (!file.type.startsWith('image/')) {
    imageFileName.textContent = 'Please choose an image file';
    setStatus('Image import failed');
    return;
  }
  imageFileName.textContent = file.name;
  const img = new Image();
  img.onload = () => {
    const offscreen = document.createElement('canvas');
    offscreen.width = SIZE;
    offscreen.height = SIZE;
    const off = offscreen.getContext('2d');
    off.fillStyle = '#000';
    off.fillRect(0, 0, SIZE, SIZE);
    const scale = Math.min(SIZE / img.width, SIZE / img.height);
    const width = Math.round(img.width * scale);
    const height = Math.round(img.height * scale);
    off.drawImage(
      img,
      Math.floor((SIZE - width) / 2),
      Math.floor((SIZE - height) / 2),
      width,
      height,
    );
    const data = off.getImageData(0, 0, SIZE, SIZE).data;
    const source = pixels.map((_, i) => {
      const r = data[i * 4].toString(16).padStart(2, '0');
      const g = data[i * 4 + 1].toString(16).padStart(2, '0');
      const b = data[i * 4 + 2].toString(16).padStart(2, '0');
      return `#${r}${g}${b}`;
    });
    imageLayer = { source, x: 0, y: 0, width: SIZE, height: SIZE };
    imageSize.value = SIZE;
    updateImageControls();
    renderImageLayer();
    setStatus('Drag image to move, adjust size, then Place');
    URL.revokeObjectURL(img.src);
  };
  img.onerror = () => {
    imageFileName.textContent = 'Could not read image';
    setStatus('Image import failed');
    URL.revokeObjectURL(img.src);
  };
  img.src = URL.createObjectURL(file);
}

imageUpload.addEventListener('change', () => {
  importImageFile(imageUpload.files?.[0]);
});

['dragenter', 'dragover'].forEach((eventName) => {
  imageDropZone.addEventListener(eventName, (event) => {
    event.preventDefault();
    imageDropZone.classList.add('dragging');
  });
});

['dragleave', 'drop'].forEach((eventName) => {
  imageDropZone.addEventListener(eventName, (event) => {
    event.preventDefault();
    imageDropZone.classList.remove('dragging');
  });
});

imageDropZone.addEventListener('drop', (event) => {
  importImageFile(event.dataTransfer?.files?.[0]);
});

imageSize.addEventListener('input', () => {
  setImageSize(imageSize.value);
});

placeImageBtn.addEventListener('click', () => {
  if (!imageLayer) return;
  recordHistory();
  pixels = imageLayerPixels();
  imageLayer = null;
  imageDragOffset = null;
  updateImageControls();
  render();
  setStatus('Image placed');
});

cancelImageBtn.addEventListener('click', () => {
  imageLayer = null;
  imageDragOffset = null;
  updateImageControls();
  render();
  setStatus('Image canceled');
});

document.getElementById('clearBtn').addEventListener('click', () => {
  recordHistory();
  pixels = pixels.map(() => '#000000');
  imageLayer = null;
  imageDragOffset = null;
  updateImageControls();
  render();
  setStatus('Canvas cleared');
});

undoBtn.addEventListener('click', () => {
  if (!undoStack.length) return;
  redoStack.push(snapshotPixels());
  pixels = undoStack.pop();
  imageLayer = null;
  imageDragOffset = null;
  updateImageControls();
  render();
  updateHistoryButtons();
  setStatus('Undo');
});

redoBtn.addEventListener('click', () => {
  if (!redoStack.length) return;
  undoStack.push(snapshotPixels());
  pixels = redoStack.pop();
  imageLayer = null;
  imageDragOffset = null;
  updateImageControls();
  render();
  updateHistoryButtons();
  setStatus('Redo');
});

document
  .getElementById('settingsBtn')
  .addEventListener('click', () => settingsDialog.showModal());

themeToggle.addEventListener('change', () => {
  const mode = themeToggle.checked ? 'dark' : 'light';
  document.documentElement.classList.toggle('dark', themeToggle.checked);
  localStorage.setItem('matrix-theme', mode);
});

espUrl.addEventListener('change', () => {
  espUrl.value = normalizeBackendUrl(espUrl.value);
  localStorage.setItem('matrix-backend-url', espUrl.value);
  loadLastFrame();
});

document.getElementById('sendBtn').addEventListener('click', async () => {
  const url = normalizeBackendUrl(espUrl.value);
  espUrl.value = url;
  localStorage.setItem('matrix-backend-url', url);
  if (!url) {
    setStatus('Add backend URL in Settings');
    settingsDialog.showModal();
    return;
  }
  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 12000);
    setStatus('Sending...');
    const response = await fetch(url, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      signal: controller.signal,
      body: JSON.stringify(matrixPayload()),
    });
    clearTimeout(timeout);
    setStatus(
      response.ok ? 'Sent to backend' : `Backend returned ${response.status}`,
    );
  } catch (error) {
    setStatus(
      error.name === 'AbortError'
        ? 'Backend timed out. Check host.'
        : 'Send failed. Check backend URL.',
    );
  }
});

render();
loadLastFrame();
