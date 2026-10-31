/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade).
 * Actual RAW gate and capture-only runtime with a synthetic Canon SDK. */
#define main bayer_unused_preview_capture_main
#include "bayer_capture.c"
#undef main
static void arm_capture(void)
{
    bayer_gate_automatic(0);fx_service();bayer_gate_automatic(1);
    bayer_raw_trace_service();bayer_gate_service();bayer_gate_report r;bayer_gate_status(&r);
    assert(r.phase==MG_ARMED&&!r.latched);
}
static void idle_camera(void)
{lv=1;lv_paused=ext_monitor_hdmi=sensor_cleaning=menu_shown=0;lv_dispsize=1;gui_state=0;lens_info.job_state=0;fx_canon_gui(0);preview_test_half=0;fx_half(0);}
int main(int argc,char**argv)
{
    (void)argv;assert(bayer_select_camera());const bayer_native_backend*n=bayer_camera_native();
    fx_camera_init();assert(fx_native_capture_mode());assert(fx_capture_scratch_configure(fx_bayer_workspace(n->width,n->pitch)));
    idle_camera();fx_params p={{4,0,0,0}};assert(fx_controls(&p));assert(fx_start());fx_service();
    fx_status status;fx_status_get(&status);assert(status.running&&!status.error&&!status.frames);
    assert(status.allocated==39600&&status.required==39600+1536u*1024u);
    unsigned char untouched[64],before[64];memset(untouched,0x5b,sizeof(untouched));memcpy(before,untouched,sizeof(before));
    struct display_filter_buffers b={untouched,untouched+32};
    assert(!fx_filter(0)&&!fx_filter(&b)&&!memcmp(untouched,before,sizeof(before)));
    assert(fx_key(MODULE_KEY_PRESS_SET)==1&&fx_key(MODULE_KEY_PLAY)==1);
    fx_state controls;assert(fx_delivered_state(&controls)&&controls.initialized&&!controls.width&&!controls.height);
    fx_capture_scratch a={0},again={0};assert(fx_capture_scratch_acquire(&a)&&fx_capture_scratch_ready(&a));
    assert(fx_capture_scratch_release(&a)&&!fx_capture_scratch_release(&a));
    assert(fx_capture_scratch_acquire(&again)&&again.generation!=a.generation&&!fx_capture_scratch_ready(&a));
    assert(!fx_capture_scratch_release(&a)&&fx_capture_scratch_release(&again));
    int*modes[]={&lv_paused,&lv_dispsize,&ext_monitor_hdmi,&menu_shown};const int invalid[]={1,5,1,1};
    for(unsigned i=0;i<4;i++){
        int old=*modes[i];*modes[i]=invalid[i];assert(!fx_delivered_state(&controls)&&!fx_capture_scratch_acquire(&a));*modes[i]=old;
        assert(fx_delivered_state(&controls));
    }
    fx_canon_gui(3);assert(!fx_delivered_state(&controls));fx_canon_gui(0);
    arm_capture();preview_test_half=1;fx_half(1);bayer_gate_half_press(1);
    bayer_gate_report r;bayer_gate_status(&r);assert(r.latched&&!memcmp(&r.params,&p,sizeof(p)));unsigned first=r.full_ms;
    fx_params pending={{8,2,1,3}};assert(fx_controls(&pending));assert(fx_delivered_state(&controls)&&!memcmp(&controls.params,&p,sizeof(p)));
    preview_test_clock+=6000;fx_service();bayer_gate_service();bayer_gate_status(&r);assert(r.latched&&r.full_ms>first);
    preview_test_half=0;fx_half(0);bayer_gate_half_press(0);assert(fx_delivered_state(&controls)&&!memcmp(&controls.params,&pending,sizeof(pending)));
    preview_test_clock+=5001;bayer_gate_service();bayer_gate_status(&r);assert(r.phase==MG_ARMED&&!r.latched&&!r.error);
    assert(fx_controls(&p));arm_capture();bayer_gate_full_press();preview_test_clock+=5001;bayer_gate_service();
    bayer_gate_status(&r);assert(r.phase==MG_FAILED&&r.error==MG_NO_SHUTTER);
    arm_capture();bayer_gate_full_press();sensor=malloc(n->sensor_bytes);assert(sensor);fill_sensor(n);
    plan_action=1;shoot(n);bayer_gate_status(&r);assert(r.phase==MG_FAILED&&r.error==MG_CANCEL&&!r.patch_started);unchanged_sensor(n);
    idle_camera();arm_capture();preview_test_half=1;fx_half(1);bayer_gate_half_press(1);
    assert(fx_controls(&pending));preview_test_half=0;fx_half(0);bayer_gate_half_press(0);
    fill_sensor(n);shoot(n);bayer_gate_status(&r);
    assert(r.phase==MG_DONE&&!r.error&&r.patch_done&&r.patch_verified==896&&!r.patch_mismatches&&r.patch_changed);
    assert(!memcmp(&r.params,&p,sizeof(p)));unsigned char*expected=malloc(n->sensor_bytes);assert(expected);memcpy(expected,sensor,n->sensor_bytes);
    idle_camera();assert(fx_controls(&p));arm_capture();bayer_gate_full_press();fill_sensor(n);write_action=1;shoot(n);bayer_gate_status(&r);
    assert(r.phase==MG_FAILED&&r.error==MG_CANCEL&&r.patch_done&&!r.patch_mismatches&&!memcmp(sensor,expected,n->sensor_bytes));
    fx_service();assert(!allocations);
    idle_camera();assert(fx_start());fx_service();arm_capture();bayer_gate_full_press();
    if(argc>1){
        fill_sensor(n);plan_action=2;shoot(n);bayer_gate_status(&r);
        assert(r.phase==MG_FAILED&&r.error==MG_SHUTDOWN&&!r.patch_started);unchanged_sensor(n);
    }else{
        fx_stop();fx_service();assert(allocations);bayer_gate_shutdown();
    }
    fx_stop();fx_service();assert(!allocations);free(expected);free(sensor);
    puts("BAYER_CAPTURE_ONLY: zero display filtering; 39600-byte allocation; control/lease validation; held recipe; physical shutter RAW; cancel, timeout and shutdown PASS");return 0;
}
