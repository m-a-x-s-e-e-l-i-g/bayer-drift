/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#include "fx_bayer.h"
#include "effect_config.h"
#include "fx_copy.h"
#include <string.h>
static int render(const fx_state*old,fx_state*next,const void*source,const void*history,void*dest,size_t cap,
    unsigned width,unsigned height,unsigned pitch,const uint32_t*times,uint32_t*new_times,
    const fx_params*p,unsigned now,void*workspace,size_t workspace_size)
{
    (void)history;(void)times;(void)new_times;
    uint64_t total=(uint64_t)pitch*height;
    if(!workspace||!source||!dest||total>FX_BAYER_PREVIEW_FRAME_BYTES||total>cap||
       workspace_size<FX_BAYER_DISPLAY_WORKSPACE||!fx_bayer_ready(workspace,workspace_size,p,width,height,pitch))return FX_INVALID;
    size_t bytes=(size_t)total;uintptr_t a=(uintptr_t)source,b=(uintptr_t)dest,c=(uintptr_t)workspace;
    if(a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-bytes||c>UINTPTR_MAX-workspace_size||
       (a<=b?b-a<bytes:a-b<bytes)||(a<=c?c-a<bytes:a-c<workspace_size)||
       (b<=c?c-b<bytes:b-c<workspace_size))return FX_INVALID;
    /* FX_INPLACE_NATIVE_PREVIEW supplies the existing private canvas as dest.
     * Freeze Canon input there, then change chroma in place. Every Y byte is
     * invariant, so later displaced reads still see the frozen source luma.
     * The camera adapter publishes this canvas only after successful render. */
    fx_copy_cpu(dest,source,bytes);
    int error=fx_bayer_preview_prepared(dest,dest,bytes,p,width,height,pitch,workspace,workspace_size);if(error)return error;
    memset(next,0,sizeof(*next));next->params=*p;next->initialized=1;next->width=width;next->height=height;
    next->last_ms=now;next->frames=old->frames+1;return FX_OK;
}
const fx_effect FX(effect)={
    .module="bayer",.title="Bayer Drift",.version=FX_VERSION,.prefix="BYR",.log_prefix="BYR",
    .kind="native-bayer-drift",.description="Intentional CFA registration interference; native Canon CR2",
    .param_count=4,.param_names={"strength","pattern","texture_size","phase"},
    .defaults={{4,0,0,0}},.workspace_size=FX_BAYER_DISPLAY_WORKSPACE,
    .validate=fx_bayer_validate,.ready=fx_bayer_ready,.prepare=fx_bayer_prepare,.render=render,
};
