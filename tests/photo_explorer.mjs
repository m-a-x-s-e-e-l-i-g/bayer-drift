import test from 'node:test';
import assert from 'node:assert/strict';
import { fittedImage, overviewPoint, viewWindow, visibleTiles } from '../site/explorer.mjs';

const landscape = { id:'scene', width:5208, height:3476, tiles:{size:512,columns:11,rows:7,template:'tiles/{x}-{y}.webp'} };
const portrait = { ...landscape, width:3476, height:5208, tiles:{...landscape.tiles,columns:7,rows:11} };

test('overview clicks map to full-photo coordinates for both orientations', () => {
  for (const photo of [landscape, portrait]) {
    const fit = fittedImage(600, 400, photo);
    const centre = overviewPoint(300,200,600,400,photo);
    assert.ok(Math.abs(centre.x-photo.width/2)<1e-9);
    assert.ok(Math.abs(centre.y-photo.height/2)<1e-9);
    const point = overviewPoint(fit.x+photo.width*fit.scale*.95,fit.y+photo.height*fit.scale*.9,600,400,photo);
    assert.ok(Math.abs(point.x-photo.width*.95)<1e-9);
    assert.ok(Math.abs(point.y-photo.height*.9)<1e-9);
    assert.deepEqual(overviewPoint(fit.x,fit.y,600,400,photo), {x:0,y:0});
  }
  assert.equal(overviewPoint(5,200,600,400,portrait),null,'portrait letterbox clicks do not move the crop');
  assert.equal(overviewPoint(300,5,600,600,landscape),null,'landscape letterbox clicks do not move the crop');
});

test('all four image edges remain reachable at every zoom', () => {
  for (const photo of [landscape,portrait]) for (const zoom of [2,4,8]) {
    const top = viewWindow(photo,0,0,600,480,zoom);
    assert.equal(top.x,0); assert.equal(top.y,0);
    const bottom = viewWindow(photo,photo.width,photo.height,600,480,zoom);
    assert.equal(bottom.x+bottom.width,photo.width);
    assert.equal(bottom.y+bottom.height,photo.height);
    for (const view of [top,bottom]) {
      const tiles = visibleTiles(photo,view);
      assert.ok(tiles.length>=1 && tiles.length<=4);
      assert.ok(tiles.every(t=>t.x>=0&&t.x<photo.tiles.columns&&t.y>=0&&t.y<photo.tiles.rows));
    }
  }
});

test('tile boundaries have no missing strip or extra request', () => {
  assert.deepEqual(visibleTiles(landscape,{x:0,y:0,width:512,height:512}).map(t=>t.key),['scene/0/0']);
  assert.deepEqual(visibleTiles(landscape,{x:500,y:500,width:100,height:100}).map(t=>t.key),
    ['scene/0/0','scene/1/0','scene/0/1','scene/1/1']);
  const far = visibleTiles(landscape,viewWindow(landscape,5208,3476,600,480,8));
  assert.ok(far.some(t=>t.url==='tiles/10-6.webp'));
});
