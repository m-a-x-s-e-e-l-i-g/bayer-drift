/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade).
 * Exercise the real camera adapter with a constrained and fragmented DMA pool.
 * Reuse capture fixture's SDK; no Canon hardware/timing claim. */
#define main bayer_unused_capture_main
#include "bayer_capture.c"
#undef main
static unsigned char*front,*previous,*canon_source,*private_canvas;
static size_t frame_bytes;static unsigned calls,armed,turnover,stop_midway,change_layout;
extern int GetFreeMemForAllocateMemory(void);
extern int GetSizeOfMaxRegion(int*);
void FX(bayer_preview_row_hook)(unsigned row,const void*destination)
{
    if(!armed)return;
    assert(row==0||row==240||row==479);
    if(!private_canvas)private_canvas=(unsigned char*)destination;
    assert(destination==private_canvas&&destination!=front&&destination!=canon_source);
    assert(!memcmp(front,previous,frame_bytes));calls++;
    if(!row&&turnover)memset(canon_source,0xee,frame_bytes);
    if(row==240&&stop_midway)fx_stop();
    if(row==240&&change_layout)lv_dispsize=5;
}
int main(void)
{
    assert(bayer_select_camera());const bayer_native_backend*n=bayer_camera_native();
    frame_bytes=1440u*480;fx_camera_init();assert(fx_native_capture_mode());
    assert(fx_capture_scratch_configure(fx_bayer_workspace(n->width,n->pitch)));fx_canon_gui(0);
    assert(fx_start());fx_service();fx_status report;fx_status_get(&report);
    assert(report.running&&!report.error&&FX(effect).workspace_size==4096);
    assert(report.allocated==frame_bytes+fx_bayer_workspace(n->width,n->pitch)+4096);
    assert(report.reserve==1536u*1024u&&report.required<=2560000u);
    assert(GetFreeMemForAllocateMemory()==2560000);
    int contiguous;assert(!GetSizeOfMaxRegion(&contiguous)&&contiguous==786432);
    /* Dev3's documented requirement cannot fit this same pool/block limit. */
    assert(report.required+2*frame_bytes>2560000u);
    assert(4096+2*frame_bytes>(size_t)contiguous-1024);
    canon_source=malloc(frame_bytes);front=malloc(frame_bytes);previous=malloc(frame_bytes);
    unsigned char*expected=malloc(frame_bytes);assert(canon_source&&front&&previous&&expected);
    for(size_t i=0;i<frame_bytes;i++)canon_source[i]=(unsigned char)((i*37)^(i>>7));
    fx_params p={{4,0,0,0}};
    assert(!fx_bayer_preview(canon_source,expected,frame_bytes,&p,720,480,1440));
    memset(front,0x6d,frame_bytes);struct display_filter_buffers b={canon_source,front};
    fx_params pending={{5,0,0,0}};assert(fx_controls(&pending));
    assert(fx_filter(0));fx_filter(&b);fx_state preparing;
    assert(!memcmp(front,canon_source,frame_bytes)&&!fx_delivered_state(&preparing));
    assert(fx_controls(&p));memcpy(previous,front,frame_bytes);armed=1;fresh(&b);
    assert(calls==3&&!memcmp(front,expected,frame_bytes));
    for(size_t i=0;i<frame_bytes;i++)assert(canon_source[i]==(unsigned char)((i*37)^(i>>7)));
    fx_capture_scratch lease={0};assert(fx_capture_scratch_acquire(&lease));
    assert(lease.data==private_canvas+frame_bytes);memset(lease.data,0x7b,lease.size);
    painting_lease=&lease;preview_test_commit_hook=while_painting;
    memcpy(previous,front,frame_bytes);calls=0;turnover=1;fresh(&b);
    assert(calls==3&&!memcmp(front,expected,frame_bytes));
    for(size_t i=0;i<frame_bytes;i++)assert(canon_source[i]==0xee);
    for(size_t i=0;i<lease.size;i++)assert(((unsigned char*)lease.data)[i]==0x7b);
    preview_test_commit_hook=0;painting_lease=0;assert(fx_capture_scratch_release(&lease));
    for(size_t i=0;i<frame_bytes;i++)canon_source[i]=(unsigned char)((i*37)^(i>>7));
    calls=0;turnover=0;change_layout=1;assert(fx_filter(0));fx_filter(&b);
    fx_state delivered;assert(calls==3&&!memcmp(front,previous,frame_bytes)&&!fx_delivered_state(&delivered));
    lv_dispsize=1;change_layout=0;calls=0;stop_midway=1;assert(fx_filter(0));fx_filter(&b);
    assert(calls==3&&!memcmp(front,previous,frame_bytes)&&!fx_delivered_state(&delivered));
    fx_service();assert(!allocations);
    free(canon_source);free(front);free(previous);free(expected);
    puts("BAYER_STAGING: 2500 KiB pool / 768 KiB largest block; original reserve; existing canvas reused; source turnover stable; display unchanged during render; RAW tail untouched; rejected frame unpublished PASS");return 0;
}
