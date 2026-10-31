/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#ifndef FX_BAYER_H
#define FX_BAYER_H
#include "fx_effect.h"
#include "glitchlock.h"
#define fx_bayer_validate FX(bayer_validate)
#define fx_bayer_axis FX(bayer_axis)
#define fx_bayer_point FX(bayer_point)
#define fx_bayer_workspace FX(bayer_workspace)
#define fx_bayer_apply FX(bayer_apply)
#define fx_bayer_prepare FX(bayer_prepare)
#define fx_bayer_ready FX(bayer_ready)
#define fx_bayer_preview FX(bayer_preview)
#define fx_bayer_preview_prepared FX(bayer_preview_prepared)
#define FX_BAYER_PREVIEW_WORKSPACE 4096u
#define FX_BAYER_PREVIEW_FRAME_BYTES (720u*480u*2u)
#define FX_BAYER_DISPLAY_WORKSPACE FX_BAYER_PREVIEW_WORKSPACE
/* strength 1..8, pattern 0 dots / 1 horizontal / 2 vertical,
 * size 0 fine / 1 medium / 2 coarse, phase 0..3; remaining words zero.
 * Tile-local same-CFA-plane reflections intentionally misregister the planes.
 * Every axis mapping is an involution, including partial boundary tiles. */
int fx_bayer_validate(const fx_params*);
unsigned fx_bayer_axis(unsigned at,unsigned begin,unsigned end,unsigned tile,unsigned phase);
void fx_bayer_point(const fx_params*,const uint32_t region[4],unsigned x,unsigned y,unsigned out[2]);
size_t fx_bayer_workspace(unsigned width,unsigned pitch);
/* Complete validation and cancellation before first write. After publishing
 * starts the entire bounded frame finishes without yielding or cancellation.
 * Private tile workspace and one packed swap row; no allocator or Canon IO. */
int fx_bayer_apply(const gl_frame*,const uint32_t region[4],const fx_params*,void*,size_t,volatile int*);
int fx_bayer_prepare(void*,size_t,const fx_params*,unsigned,unsigned,unsigned,volatile int*);
int fx_bayer_ready(const void*,size_t,const fx_params*,unsigned,unsigned,unsigned);
int fx_bayer_preview(const void*,void*,size_t,const fx_params*,unsigned,unsigned,unsigned);
int fx_bayer_preview_prepared(const void*,void*,size_t,const fx_params*,unsigned,unsigned,unsigned,const void*,size_t);
#endif
