import { fittedImage, overviewPoint, viewWindow, visibleTiles } from './explorer.mjs';

const sampleButtons = [...document.querySelectorAll('[data-sample]')];
const zoomButtons = [...document.querySelectorAll('[data-zoom]')];
const canvas = document.querySelector('#pixel-canvas');
const context = canvas.getContext('2d');
const overview = document.querySelector('#overview-image');
const viewport = document.querySelector('#source-viewport');
const marker = document.querySelector('#crop-marker');
const fallback = document.querySelector('#pixel-fallback');
const status = document.querySelector('#viewer-status');
const loading = document.querySelector('#pixel-loading');
const state = { photos: [], photo: null, zoom: 4, cx: 0, cy: 0, sequence: 0, ready: false, frame: 0, view: null };
const cache = new Map();
const MAX_TILES = 12; // At most 12 MiB of decoded 512x512 RGBA tile pixels.

function requestDraw() {
  if (!state.frame) state.frame = requestAnimationFrame(() => { state.frame = 0; draw(); });
}

function discard(key, tile) {
  cache.delete(key);
  tile.disposed = true;
  tile.controller.abort();
  tile.image?.close();
}

function tileAt(spec) {
  let tile = cache.get(spec.key);
  if (tile) { cache.delete(spec.key); cache.set(spec.key, tile); return tile; }
  tile = { controller: new AbortController(), image: null, error: false, disposed: false };
  cache.set(spec.key, tile);
  fetch(spec.url, { signal: tile.controller.signal }).then(response => {
    if (!response.ok) throw new Error('Tile unavailable');
    return response.blob();
  }).then(blob => createImageBitmap(blob)).then(image => {
    if (tile.disposed) { image.close(); return; }
    tile.image = image;
    requestDraw();
  }).catch(error => {
    if (tile.disposed || error.name === 'AbortError') return;
    tile.error = true;
    requestDraw();
  });
  return tile;
}

function draw() {
  if (!state.ready || !context) return;
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.round(rect.width * dpr), height = Math.round(rect.height * dpr);
  if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
  const photo = state.photo;
  const view = viewWindow(photo, state.cx, state.cy, rect.width, rect.height, state.zoom);
  state.cx = view.cx; state.cy = view.cy; state.view = view;
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  context.clearRect(0, 0, rect.width, rect.height);
  context.imageSmoothingEnabled = false;
  // Immediate spatial feedback while the requested native pixels arrive.
  context.drawImage(overview, view.x / photo.width * overview.naturalWidth,
    view.y / photo.height * overview.naturalHeight,
    view.width / photo.width * overview.naturalWidth, view.height / photo.height * overview.naturalHeight,
    0, 0, rect.width, rect.height);
  const specs = visibleTiles(photo, view), needed = new Set(specs.map(tile => tile.key));
  for (const [key, tile] of cache) if (!tile.image && !tile.error && !needed.has(key)) discard(key, tile);
  let pending = 0, errors = 0;
  for (const spec of specs) {
    const tile = tileAt(spec);
    if (!tile.image) { pending++; if (tile.error) errors++; continue; }
    const tx = spec.x * photo.tiles.size, ty = spec.y * photo.tiles.size;
    const left = Math.max(view.x, tx), top = Math.max(view.y, ty);
    const right = Math.min(view.x + view.width, tx + tile.image.width);
    const bottom = Math.min(view.y + view.height, ty + tile.image.height);
    context.drawImage(tile.image, left - tx, top - ty, right - left, bottom - top,
      (left - view.x) * state.zoom, (top - view.y) * state.zoom,
      (right - left) * state.zoom, (bottom - top) * state.zoom);
  }
  for (const [key, tile] of cache) {
    if (cache.size <= MAX_TILES) break;
    if (!needed.has(key)) discard(key, tile);
  }
  const fit = fittedImage(viewport.clientWidth, viewport.clientHeight, photo);
  marker.style.left = `${fit.x + view.x * fit.scale}px`;
  marker.style.top = `${fit.y + view.y * fit.scale}px`;
  marker.style.width = `${view.width * fit.scale}px`;
  marker.style.height = `${view.height * fit.scale}px`;
  marker.hidden = false;
  loading.hidden = pending === 0;
  loading.textContent = errors ? 'Preview only · detail unavailable' : 'Loading native pixels…';
  status.hidden = errors === 0;
  if (errors) status.textContent = 'Some detail could not load. Re-select the photograph to retry.';
  document.querySelector('#pixel-caption').textContent = `${Math.round(view.width)} × ${Math.round(view.height)} pixels · ${state.zoom}× display`;
  canvas.setAttribute('aria-label', `${photo.label}, magnified ${state.zoom} times. Drag or use arrow keys to explore the whole photograph. Shift moves farther; Home resets the view.`);
}

function resetView() {
  if (!state.photo) return;
  state.cx = state.photo.patch.x + state.photo.patch.size / 2;
  state.cy = state.photo.patch.y + state.photo.patch.size / 2;
  requestDraw();
}

async function select(id) {
  const photo = state.photos.find(item => item.id === id);
  if (!photo) return;
  const sequence = ++state.sequence;
  state.ready = false; state.photo = photo;
  for (const [key, tile] of cache) if (!tile.image) discard(key, tile);
  marker.hidden = true; canvas.hidden = true; loading.hidden = true;
  fallback.src = photo.detail;
  fallback.alt = `Enlarged detail from ${photo.label.toLowerCase()}`;
  fallback.hidden = false;
  overview.src = photo.wide; overview.alt = photo.alt;
  overview.width = photo.width; overview.height = photo.height;
  document.querySelector('#sample-title').textContent = photo.title;
  document.querySelector('#sample-file').textContent = photo.original;
  document.querySelector('#source-size').textContent = `${photo.width} × ${photo.height} developed pixels`;
  for (const button of sampleButtons) button.setAttribute('aria-pressed', String(button.dataset.sample === id));
  status.hidden = true;
  try {
    await overview.decode();
    if (sequence !== state.sequence) return;
    if (!context || !photo.tiles) throw new Error('Interactive viewer unavailable');
    state.ready = true;
    canvas.hidden = false; fallback.hidden = true;
    resetView();
  } catch {
    if (sequence !== state.sequence) return;
    status.textContent = 'The interactive view could not load. The fixed enlarged crop is shown instead.';
    status.hidden = false;
  }
}

let starting;
function start() {
  if (starting) return starting;
  starting = fetch('assets/photos/photos.json?v=full-photo-1').then(response => {
    if (!response.ok) throw new Error('Gallery unavailable');
    return response.json();
  }).then(data => { state.photos = data.photos; return select('bark'); }).catch(() => {
    starting = undefined;
    status.textContent = 'The gallery data could not load. The original photograph and fixed detail remain available.';
    status.hidden = false;
  });
  return starting;
}
for (const button of sampleButtons) button.addEventListener('click', async () => { await start(); await select(button.dataset.sample); });
for (const button of zoomButtons) button.addEventListener('click', async () => {
  await start(); state.zoom = Number(button.dataset.zoom);
  for (const option of zoomButtons) option.setAttribute('aria-pressed', String(option === button));
  requestDraw();
});
document.querySelector('#reset-detail').addEventListener('click', resetView);

function moveFromOverview(event, clamp = false) {
  const rect = viewport.getBoundingClientRect();
  const point = overviewPoint(event.clientX - rect.left, event.clientY - rect.top,
    viewport.clientWidth, viewport.clientHeight, state.photo, clamp);
  if (!point) return false;
  state.cx = point.x; state.cy = point.y; requestDraw(); return true;
}
let overviewPointer;
viewport.addEventListener('pointerdown', event => {
  if (!state.ready || event.button !== 0) return;
  if (event.pointerType === 'mouse' && !moveFromOverview(event)) return;
  overviewPointer = { id: event.pointerId, type: event.pointerType, x: event.clientX, y: event.clientY };
  if (event.pointerType === 'mouse') { viewport.setPointerCapture(event.pointerId); viewport.focus({ preventScroll: true }); }
});
viewport.addEventListener('pointermove', event => {
  if (overviewPointer?.id === event.pointerId && overviewPointer.type === 'mouse') moveFromOverview(event, true);
});
viewport.addEventListener('pointerup', event => {
  if (overviewPointer?.id !== event.pointerId) return;
  if (overviewPointer.type !== 'mouse' && Math.hypot(event.clientX-overviewPointer.x,event.clientY-overviewPointer.y)<10) moveFromOverview(event);
  overviewPointer = null;
});
for (const name of ['pointercancel','lostpointercapture']) viewport.addEventListener(name, () => { overviewPointer = null; });
let pointer;
canvas.addEventListener('pointerdown', event => {
  if (!state.ready || event.button !== 0) return;
  pointer = { id:event.pointerId, x:event.clientX, y:event.clientY };
  canvas.setPointerCapture(event.pointerId); canvas.focus({ preventScroll:true });
});
canvas.addEventListener('pointermove', event => {
  if (!pointer || pointer.id !== event.pointerId) return;
  state.cx -= (event.clientX-pointer.x)/state.zoom; state.cy -= (event.clientY-pointer.y)/state.zoom;
  pointer.x = event.clientX; pointer.y = event.clientY; requestDraw();
});
for (const name of ['pointerup','pointercancel','lostpointercapture']) canvas.addEventListener(name, () => { pointer = null; });
function keyPan(event) {
  if (!state.ready) return;
  if (event.key === 'Home') { event.preventDefault(); resetView(); return; }
  const directions = { ArrowLeft:[-1,0], ArrowRight:[1,0], ArrowUp:[0,-1], ArrowDown:[0,1] };
  if (!directions[event.key]) return;
  event.preventDefault();
  const [x,y] = directions[event.key], view = state.view;
  if (event.ctrlKey || event.metaKey) {
    if (x) state.cx = x<0 ? 0 : state.photo.width;
    if (y) state.cy = y<0 ? 0 : state.photo.height;
  } else {
    const step = event.shiftKey ? .75 : .12;
    state.cx += x*view.width*step; state.cy += y*view.height*step;
  }
  requestDraw();
}
canvas.addEventListener('keydown',keyPan); viewport.addEventListener('keydown',keyPan);
new ResizeObserver(requestDraw).observe(viewport);
new ResizeObserver(requestDraw).observe(canvas.parentElement);
if ('IntersectionObserver' in window) {
  const observer = new IntersectionObserver(entries => {
    if (entries.some(entry=>entry.isIntersecting)) { observer.disconnect(); start(); }
  }, {rootMargin:'240px'});
  observer.observe(document.querySelector('#inspector'));
} else start();
