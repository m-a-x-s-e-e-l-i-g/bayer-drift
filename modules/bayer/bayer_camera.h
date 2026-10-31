/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#ifndef BAYER_CAMERA_H
#define BAYER_CAMERA_H
#include <stdint.h>
#include "fx_namespace.h"
#define bayer_select_camera FX(camera_select)
#define bayer_camera_name FX(camera_name)
#define bayer_camera_native FX(camera_native)
#define bayer_camera_dma FX(camera_dma)
#define BAYER_NATIVE_POINTS 8u
enum {BAYER_DMA_DIGIC45=1,BAYER_DMA_DIGIC5=BAYER_DMA_DIGIC45};
typedef struct {uint32_t pc,words[4],kind;} bayer_hook;
/* Module-owned data, never a Canon/DryOS or camera-dependent core struct. */
typedef struct {
    uint32_t stage_pc,stage_words[4],stage_context,stage_caller,stage_job_offset;
    uint32_t stage_cpsr_mask,stage_cpsr_value,source_alignment_mask;
    bayer_hook points[BAYER_NATIVE_POINTS];
    uint32_t point_count,trace_start_pc,dma_kind;
    uint32_t raw_desc,sss_desc,jpeg_desc,raw_context,sss_context;
    uint32_t alias_bit,origin_offset,width,height,pitch,sensor_bytes;
    uint32_t readout_offset,readout_bytes,readout_xn,readout_flags;
    uint32_t region[4];
    /* Saved callee-preserved register containing the capture context. The
     * 600D uses r11; EOS M / 650D / 70D paths use r10. No backend is inferred
     * from this field alone. Appended to preserve earlier field offsets. */
    uint32_t stage_context_register;
} bayer_native_backend;
static inline int bayer_stage_context_register_valid(uint32_t reg)
{return reg>=4u&&reg<=11u;}
typedef struct {
    uint32_t off1a,off1b,off2a,off2b,off3,xa,xb,ya,yb,xn,yn;
} bayer_dma_info;
/* Select once at module init, before tasks, handlers, or memory access.
 * Preview candidates are explicitly listed; native support is independent. */
int bayer_select_camera(void);
const char *bayer_camera_name(void);
const bayer_native_backend *bayer_camera_native(void);
int bayer_camera_dma(unsigned channel,bayer_dma_info *out);
/* Core scalar interfaces have no per-camera struct-return ABI. */
uint32_t edmac_get_flags(uint32_t channel);
uint32_t edmac_get_state(uint32_t channel);
uint32_t edmac_get_address(uint32_t channel);
uint32_t edmac_get_pointer(uint32_t channel);
int edmac_fix_off1(int32_t off);
int edmac_fix_off2(int32_t off);
#endif
