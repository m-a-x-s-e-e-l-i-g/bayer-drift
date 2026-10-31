/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#ifdef BAYER_CAMERA_HOST_TEST
#include "mirror_camera_sdk.h"
#else
#include <module.h>
#include <dryos.h>
#endif
#include "bayer_camera.h"
#include "bayer_raw_trace.h"
/* EOSM 2.0.2 only. Hook/readout values retained from the camera-tested 0.2.0 adapter.
 * A new backend needs its own verified hooks, geometry and ownership checks;
 * sharing a processor or sensor resolution is not sufficient. */
static const bayer_native_backend eosm202={
    .stage_pc=0xff3608d8u,
    .stage_words={0xe12fff1eu,0xe3a00000u,0xe12fff1eu,0xe12fff1eu},
    .stage_context=0x3f3d4u,.stage_caller=0xff152238u,.stage_job_offset=0xb4u,.stage_context_register=10,
    .stage_cpsr_mask=0xdfu,.stage_cpsr_value=0x13u,
    .points={
        {0xff1707e8u,{0xe5860058u,0xe5867088u,0xe5869070u,0xe5950094u},MT_RAW_SOURCE},
        {0xff1686d0u,{0xe5870058u,0xe587a070u,0xe587b088u,0xe5960050u},MT_SSS_SOURCE},
        {0xff429210u,{0xe92d407cu,0xe51f5edcu,0xe1a04000u,0xe5950004u},MT_PREPARE},
        {0xff4292b8u,{0xe92d4070u,0xe1a05000u,0xe51f0f88u,0xe3a04000u},MT_START},
        {0xff168128u,{0xe5860058u,0xe59d0000u,0xe2800b11u,0xe2800f41u},MT_JPEG_SOURCE},
        {0xff429280u,{0xe92d4070u,0xe1a05000u,0xe51f0f50u,0xe3a04000u},MT_PREPARE_JPEG},
        {0xff22dac0u,{0xe5900000u,0xe5922000u,0xe2811e25u,0xe1500002u},MT_JOB_GET},
        {0xff22da74u,{0xe92d4010u,0xe51f36f4u,0xe5902004u,0xe5900000u},MT_JOB_SET},
    },
    .point_count=8,.trace_start_pc=0xff4292b8u,.dma_kind=BAYER_DMA_DIGIC45,
    .raw_desc=0x70340u,.sss_desc=0x6fce4u,.jpeg_desc=0x6fd8cu,
    .raw_context=0x40334u,.sss_context=0x4018cu,
    .alias_bit=0x40000000u,.origin_offset=0x4830u,
    .width=5280,.height=3528,.pitch=9240,.sensor_bytes=32598720u,
    .readout_offset=0x9060u,.readout_bytes=32607960u,.readout_xn=3528,.readout_flags=0,
    /* Full decoder-visible RAW area, including the image pixels outside
     * Canon's 5184x3456 JPEG crop (64,84)-(3520,5268). The owner CR2 and
     * LibRaw's EOS M geometry agree on 5208x3476 at (72,52). Leave the
     * optical-black top/left margins untouched. End coordinates exclude. */
    .region={52,72,3528,5280},
};
typedef struct {
    const char *model,*firmware;
    const bayer_native_backend *native;
} camera_profile;
/* Bayer Drift is intentionally restricted to the original EOS M 2.0.2. */
static const camera_profile profiles[]={{"EOSM","2.0.2",&eosm202}};
static const camera_profile *selected;
int bayer_select_camera(void)
{
    selected=0;
    for(unsigned i=0;i<sizeof(profiles)/sizeof(profiles[0]);i++){
        if(is_camera(profiles[i].model,profiles[i].firmware)){
            selected=&profiles[i];return 1;
        }
    }
    return 0;
}
const char *bayer_camera_name(void){return selected?selected->model:"Unsupported";}
const bayer_native_backend *bayer_camera_native(void)
{
#ifdef BAYER_PREVIEW_ONLY
    /* Older module APIs may provide display filters but no patch manager.
     * This separately built variant never exposes a native capture backend. */
    return 0;
#else
    return selected?selected->native:0;
#endif
}
int bayer_camera_dma(unsigned channel,bayer_dma_info *out)
{
#ifdef BAYER_PREVIEW_ONLY
    (void)channel;(void)out;return 0;
#else
    /* Keep the DIGIC 4/5 register layout explicit, but delegate signed offset
     * widths to the running core's scalar API (D4: 17/28; D5: 19/32 bits).
     * Never call edmac_get_info: its struct-return ABI changes on later cores.
     * This path requires a selected native backend and performs no MMIO writes. */
    const bayer_native_backend*backend=bayer_camera_native();
    if(!out||!backend||backend->dma_kind!=BAYER_DMA_DIGIC45||channel>=32)return 0;
    extern uint32_t edmac_get_base(uint32_t channel);
    uint32_t base=edmac_get_base(channel);
    if(!base||base==UINT32_MAX)return 0;
    uint32_t a=shamem_read(base+0x14),b=shamem_read(base+0x10),n=shamem_read(base+0x0c);
    *out=(bayer_dma_info){
        .xa=a&65535,.ya=a>>16,.xb=b&65535,.yb=b>>16,.xn=n&65535,.yn=n>>16,
        .off1a=(uint32_t)edmac_fix_off1((int32_t)shamem_read(base+0x20)),
        .off1b=(uint32_t)edmac_fix_off1((int32_t)shamem_read(base+0x18)),
        .off2a=(uint32_t)edmac_fix_off2((int32_t)shamem_read(base+0x24)),
        .off2b=(uint32_t)edmac_fix_off2((int32_t)shamem_read(base+0x1c)),
        .off3=(uint32_t)edmac_fix_off2((int32_t)shamem_read(base+0x28)),
    };
    return 1;
#endif
}
