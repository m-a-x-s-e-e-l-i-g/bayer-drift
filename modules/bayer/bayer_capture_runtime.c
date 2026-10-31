/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade).
 * Capture-only runtime. No display callback, pixels, LCD buffers or preview
 * allocation. The legacy fx_* names are the guarded capture adapter boundary. */
#ifdef FX_HOST_TEST
#include "fx_sdk.h"
#else
#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <property.h>
#include <lens.h>
#include <vram.h>
#include <arm-mcr.h>
#include <config.h>
#endif
#include <string.h>
#include "fx_camera.h"
#include "fx_bayer.h"
#include "gl_format.h"
#include "gl_memory_ml.h"
static struct {
    fx_status report;fx_params desired,held;
    void*raw;void*workspace;size_t bytes;
    unsigned requested,starting,stopping,leased,generation,canon_gui;
    volatile unsigned stop;
} capture;
static unsigned lock(void){unsigned s=cli();__asm__ __volatile__("":::"memory");return s;}
static void unlock(unsigned s){__asm__ __volatile__("":::"memory");sei(s);}
static int usable(void)
{
    return lv&&!lv_paused&&lv_dispsize==1&&!ext_monitor_hdmi&&!sensor_cleaning&&!ml_shutdown_requested&&
        !gui_menu_shown()&&capture.canon_gui!=1&&capture.canon_gui!=3&&capture.canon_gui!=12;
}
static int family(const char*n)
{return !strcmp(n,"glock")||!strcmp(n,"slitscan")||!strcmp(n,"mirror")||!strcmp(n,"kaleido")||
        !strcmp(n,"feedback")||!strcmp(n,"fragment")||!strcmp(n,"diffract")||!strcmp(n,"bayer");}
static int claim(void)
{
    const int*busy[8];unsigned count=0;
    for(int id=module_get_next_loaded(-1);id>=0;id=module_get_next_loaded(id)){
        const char*n=module_get_name(id);
        if(n&&family(n)&&strcmp(n,"bayer")){
            char key[64];gl_snprintf(key,sizeof(key),"maxmade.fx.%s.busy",n);
            struct config_var*v=module_get_config_var(key);
            if(!v||!v->value||count>=8)return 0;
            busy[count++]=v->value;
        }
    }
    unsigned s=lock();
    for(unsigned i=0;i<count;i++)if(*busy[i]){unlock(s);return 0;}
    if(FX(active)){unlock(s);return 0;}
    FX(active)=1;unlock(s);return 1;
}
void fx_camera_init(void)
{memset(&capture,0,sizeof(capture));capture.desired=(fx_params){{4,0,0,0}};FX(active)=0;}
int fx_native_capture_mode(void)
{
    unsigned s=lock();int valid=!FX(active)&&!capture.raw&&!capture.starting;
    if(valid)capture.report.native_capture=1;
    unlock(s);return valid;
}
int fx_capture_scratch_configure(size_t bytes)
{
    unsigned s=lock();int valid=capture.report.native_capture&&!FX(active)&&!capture.raw&&!capture.starting&&
        !capture.leased&&bytes>0&&bytes<=4u*1024u*1024u;
    if(valid)capture.bytes=bytes;
    unlock(s);return valid;
}
int fx_start(void)
{
    if(FX(active))return 0;
    if(!claim()){capture.report.error=11;return 0;}
    unsigned s=lock();capture.stop=0;capture.requested=1;capture.report.error=0;unlock(s);return 1;
}
void fx_stop(void){capture.stop=1;}
int fx_controls(const fx_params*p)
{if(!fx_bayer_validate(p))return 0;unsigned s=lock();capture.desired=*p;unlock(s);return 1;}
void fx_canon_gui(unsigned state){unsigned s=lock();capture.canon_gui=state;unlock(s);}
void fx_half(int down)
{
    unsigned s=lock();down=down!=0;
    if(capture.report.half!=(unsigned)down){
        capture.report.half=(unsigned)down;
        capture.report.frozen=down&&capture.report.running&&!capture.stop&&usable();
        if(capture.report.frozen)capture.held=capture.desired;
    }
    unlock(s);
}
unsigned int fx_filter(void*context){(void)context;return 0;}
int fx_key(unsigned key){if(key==MODULE_KEY_PRESS_HALFSHUTTER)fx_half(1);return 1;}
void fx_status_get(fx_status*out)
{if(out){unsigned s=lock();*out=capture.report;unlock(s);}}
int fx_delivered_state(fx_state*out)
{
    /* Capture-only compatibility API: validated controls, never image delivery.
     * The first physical half-press freezes the recipe until release. */
    if(!out)return 0;
    unsigned s=lock();int valid=capture.report.running&&!capture.stop&&!capture.starting&&usable();
    if(valid){memset(out,0,sizeof(*out));out->params=capture.report.frozen?capture.held:capture.desired;
        out->initialized=1;out->last_ms=get_ms_clock();}
    unlock(s);return valid;
}
static int token(const fx_capture_scratch*t)
{return t&&capture.leased&&t->data==capture.workspace&&t->size==capture.bytes&&t->generation==capture.generation;}
int fx_capture_scratch_acquire(fx_capture_scratch*out)
{
    if(!out)return 0;
    unsigned s=lock();int valid=capture.report.running&&!capture.stop&&!capture.starting&&!capture.stopping&&
        !capture.leased&&capture.workspace&&usable();
    if(valid){capture.leased=1;if(!++capture.generation)++capture.generation;
        *out=(fx_capture_scratch){capture.workspace,capture.bytes,capture.generation};}
    unlock(s);return valid;
}
int fx_capture_scratch_ready(const fx_capture_scratch*t)
{unsigned s=lock();int ready=token(t)&&!capture.starting&&!capture.stopping;unlock(s);return ready;}
int fx_capture_scratch_release(const fx_capture_scratch*t)
{unsigned s=lock();int valid=token(t);if(valid)capture.leased=0;unlock(s);return valid;}
void fx_service(void)
{
    if(ml_shutdown_requested)capture.stop=1;
    fx_half(get_halfshutter_pressed());
    unsigned s=lock();unsigned start=capture.requested&&!capture.starting&&!capture.stopping;
    if(start){capture.requested=0;capture.starting=1;}
    unlock(s);
    if(start){
        int available=GetFreeMemForAllocateMemory();void*raw=0;unsigned error=0;
        capture.report.free_initial=available<0?0:(unsigned)available;
        capture.report.reserve=1536u*1024u;capture.report.required=(unsigned)capture.bytes+capture.report.reserve;
        capture.report.allocation_stage=1;
        if(!capture.report.native_capture||!capture.bytes||available<0||(unsigned)available<capture.report.required)error=10;
        else if(module_display_filter_enabled())error=11;
        else if(!capture.stop){capture.report.allocation_stage=2;raw=gl_display_alloc(capture.bytes);if(!raw)error=10;}
        s=lock();capture.starting=0;
        if(!error&&!capture.stop&&raw){
            capture.raw=raw;capture.workspace=CACHEABLE(raw);raw=0;capture.report.running=1;
            capture.report.allocated=(unsigned)capture.bytes;
            if(capture.report.allocated>capture.report.peak)capture.report.peak=capture.report.allocated;
            capture.report.allocation_stage=0;
        }else{capture.report.error=error;capture.stop=1;}
        unlock(s);if(raw)gl_display_free(raw);
    }
    s=lock();unsigned cleanup=capture.stop&&FX(active)&&!capture.starting&&!capture.stopping&&!capture.leased;
    void*old=0;
    if(cleanup){capture.stopping=1;capture.report.running=0;old=capture.raw;capture.raw=capture.workspace=0;}
    unlock(s);
    if(cleanup){
        if(old)gl_display_free(old);
        s=lock();capture.report.allocated=0;capture.report.half=capture.report.frozen=0;
        capture.stopping=0;FX(active)=0;unlock(s);
    }
}
