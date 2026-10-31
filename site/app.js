const sampleButtons = [...document.querySelectorAll('[data-sample]')];
const zoomButtons = [...document.querySelectorAll('[data-zoom]')];
const canvas = document.querySelector('#pixel-canvas');
const context = canvas.getContext('2d');
const overview = document.querySelector('#overview-image');
const viewport = document.querySelector('#source-viewport');
const marker = document.querySelector('#crop-marker');
const fallback = document.querySelector('#pixel-fallback');
const status = document.querySelector('#viewer-status');
const state = { photos: [], photo: null, image: null, zoom: 4, cx: 256, cy: 256, sequence: 0, loaded: false };
const cache = new Map();

function imageAt(url) {
  if (!cache.has(url)) {
    const image = new Image();
    image.src = url;
    const ready = image.decode().then(() => image).catch(error => { cache.delete(url); throw error; });
    cache.set(url, ready);
  }
  return cache.get(url);
}

function draw() {
  if (!state.image || !state.photo || !context) return;
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.round(rect.width * dpr);
  canvas.height = Math.round(rect.height * dpr);
  const sw = rect.width / state.zoom, sh = rect.height / state.zoom;
  const edge = state.photo.patch.size;
  state.cx = Math.max(sw / 2, Math.min(edge - sw / 2, state.cx));
  state.cy = Math.max(sh / 2, Math.min(edge - sh / 2, state.cy));
  const x = state.cx - sw / 2, y = state.cy - sh / 2;
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  context.imageSmoothingEnabled = false;
  context.drawImage(state.image, x, y, sw, sh, 0, 0, rect.width, rect.height);
  const photo = state.photo;
  const scale = Math.min(viewport.clientWidth / photo.width, viewport.clientHeight / photo.height);
  marker.style.left = `${(viewport.clientWidth - photo.width * scale) / 2 + (photo.patch.x + x) * scale}px`;
  marker.style.top = `${(viewport.clientHeight - photo.height * scale) / 2 + (photo.patch.y + y) * scale}px`;
  marker.style.width = `${sw * scale}px`;
  marker.style.height = `${sh * scale}px`;
  marker.hidden = false;
  document.querySelector('#pixel-caption').textContent = `${Math.round(sw)} × ${Math.round(sh)} pixels · ${state.zoom}× display`;
  canvas.setAttribute('aria-label', `${photo.label}, magnified ${state.zoom} times. Drag or use arrow keys to move within the detail patch.`);
}

async function select(id) {
  const photo = state.photos.find(item => item.id === id);
  if (!photo) return;
  const sequence = ++state.sequence;
  state.image = null;
  marker.hidden = true;
  canvas.hidden = true;
  fallback.src = photo.detail;
  fallback.alt = `Enlarged detail from ${photo.label.toLowerCase()}`;
  fallback.hidden = false;
  overview.src = photo.wide;
  overview.alt = photo.alt;
  overview.width = photo.width;
  overview.height = photo.height;
  document.querySelector('#sample-title').textContent = photo.title;
  document.querySelector('#sample-file').textContent = photo.original;
  document.querySelector('#source-size').textContent = `${photo.width} × ${photo.height} developed pixels`;
  for (const button of sampleButtons) button.setAttribute('aria-pressed', String(button.dataset.sample === id));
  status.hidden = true;
  try {
    const [image] = await Promise.all([imageAt(photo.pixels), overview.decode()]);
    if (sequence !== state.sequence) return;
    state.photo = photo;
    state.image = image;
    state.cx = state.cy = photo.patch.size / 2;
    canvas.hidden = false;
    fallback.hidden = true;
    draw();
  } catch {
    if (sequence !== state.sequence) return;
    status.textContent = 'The interactive detail could not load. The fixed enlarged crop is shown instead.';
    status.hidden = false;
  }
}

let starting;
function start() {
  if (starting) return starting;
  starting = fetch('assets/photos/photos.json').then(response => {
    if (!response.ok) throw new Error('Gallery unavailable');
    return response.json();
  }).then(data => {
    state.photos = data.photos;
    state.loaded = true;
    return select('bark');
  }).catch(() => {
    starting = undefined;
    status.textContent = 'The gallery data could not load. The original photograph and fixed detail remain available.';
    status.hidden = false;
  });
  return starting;
}

for (const button of sampleButtons) button.addEventListener('click', async () => { await start(); await select(button.dataset.sample); });
for (const button of zoomButtons) button.addEventListener('click', async () => {
  await start();
  state.zoom = Number(button.dataset.zoom);
  for (const option of zoomButtons) option.setAttribute('aria-pressed', String(option === button));
  draw();
});
document.querySelector('#reset-detail').addEventListener('click', () => { state.cx = state.cy = 256; draw(); });
let pointer;
canvas.addEventListener('pointerdown', event => {
  if (!state.image) return;
  pointer = { id: event.pointerId, x: event.clientX, y: event.clientY };
  canvas.setPointerCapture(event.pointerId);
  canvas.focus({ preventScroll: true });
});
canvas.addEventListener('pointermove', event => {
  if (!pointer || pointer.id !== event.pointerId) return;
  state.cx -= (event.clientX - pointer.x) / state.zoom;
  state.cy -= (event.clientY - pointer.y) / state.zoom;
  pointer.x = event.clientX; pointer.y = event.clientY;
  draw();
});
for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) canvas.addEventListener(name, () => { pointer = null; });
canvas.addEventListener('keydown', event => {
  const directions = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
  if (!directions[event.key]) return;
  event.preventDefault();
  const [x, y] = directions[event.key];
  state.cx += x * (event.shiftKey ? 24 : 6);
  state.cy += y * (event.shiftKey ? 24 : 6);
  draw();
});
new ResizeObserver(draw).observe(viewport);
new ResizeObserver(draw).observe(canvas.parentElement);
if ('IntersectionObserver' in window) {
  const observer = new IntersectionObserver(entries => {
    if (entries.some(entry => entry.isIntersecting)) { observer.disconnect(); start(); }
  }, { rootMargin: '240px' });
  observer.observe(document.querySelector('#inspector'));
} else start();
