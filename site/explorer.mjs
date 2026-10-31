// Geometry is expressed in pixels of the full-resolution RGB development.
export function fittedImage(width, height, photo) {
  const scale = Math.min(width / photo.width, height / photo.height);
  return { x: (width - photo.width * scale) / 2, y: (height - photo.height * scale) / 2, scale };
}

export function overviewPoint(x, y, width, height, photo, clamp = false) {
  const fit = fittedImage(width, height, photo);
  const px = (x - fit.x) / fit.scale, py = (y - fit.y) / fit.scale;
  if (!clamp && (px < 0 || py < 0 || px > photo.width || py > photo.height)) return null;
  return { x: Math.max(0, Math.min(photo.width, px)), y: Math.max(0, Math.min(photo.height, py)) };
}

export function viewWindow(photo, cx, cy, width, height, zoom) {
  const w = Math.min(photo.width, width / zoom), h = Math.min(photo.height, height / zoom);
  const x = Math.max(0, Math.min(photo.width - w, cx - w / 2));
  const y = Math.max(0, Math.min(photo.height - h, cy - h / 2));
  return { x, y, width: w, height: h, cx: x + w / 2, cy: y + h / 2 };
}

export function visibleTiles(photo, view) {
  const size = photo.tiles.size, result = [];
  const lastX = Math.min(photo.tiles.columns - 1, Math.ceil((view.x + view.width) / size) - 1);
  const lastY = Math.min(photo.tiles.rows - 1, Math.ceil((view.y + view.height) / size) - 1);
  for (let y = Math.floor(view.y / size); y <= lastY; y++) {
    for (let x = Math.floor(view.x / size); x <= lastX; x++) {
      result.push({ key: `${photo.id}/${x}/${y}`, x, y,
        url: photo.tiles.template.replace('{x}', x).replace('{y}', y) });
    }
  }
  return result;
}
