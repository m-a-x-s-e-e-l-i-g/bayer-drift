/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#ifdef FX_BAYER_UI_HOST_TEST
#include "mirror_ui_sdk.h"
#else
#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <property.h>
#include <config.h>
#include <lens.h>
#endif
#include "fx_camera.h"
#include "effect_config.h"
#include "bayer_raw_trace.h"
#include "bayer_capture_gate.h"
#include "bayer_settings.h"
#include "bayer_camera.h"
#include "gl_format.h"
CONFIG_INT("maxmade.fx.bayer.busy",bayer_active,0);
static volatile int shutting_down,finished;
static bayer_settings preferences={0,1,4,0,0,0};
static bayer_settings applied_preferences;
static unsigned attempted,started_ms,dirty,changed_ms,save_error;
static const char*presets[]={"Subtle","Crunchy","Overdrive","Custom"};
static const char*patterns[]={"Dots","Horizontal","Vertical"};
static const char*sizes[]={"Fine","Medium","Coarse"};
static const char*phases[]={"1","2","3","4"};
static struct menu_entry entries[];
static void synchronize_settings(unsigned now)
{
    /* Core pickbox/script setters may write entry->priv without calling select.
     * Reconcile in the worker as well as the physical-menu callbacks. Keep the
     * preference snapshot and desired renderer controls coherent; never keep
     * interrupts disabled across capture cleanup, allocation or file access. */
    unsigned irq=cli();bayer_settings next=preferences;
    if(next.preset!=applied_preferences.preset&&next.preset<3)bayer_settings_preset(&next,next.preset);
    else if(next.strength!=applied_preferences.strength||next.pattern!=applied_preferences.pattern||
            next.size!=applied_preferences.size||next.phase!=applied_preferences.phase)next.preset=3;
    if(!bayer_settings_valid(&next)){preferences=applied_preferences;sei(irq);return;}
    preferences=next;
    if(bayer_settings_equal(&next,&applied_preferences)){sei(irq);return;}
    unsigned enable_changed=next.enabled!=applied_preferences.enabled;
    fx_params p=bayer_settings_params(&next);fx_controls(&p);
    applied_preferences=next;
    if(enable_changed)attempted=0;
    dirty=1;changed_ms=now;sei(irq);
    if(enable_changed&&!preferences.enabled){bayer_gate_automatic(0);bayer_raw_trace_cancel();fx_stop();}
}
static void service_once(void)
{
    unsigned now=get_ms_clock();
    synchronize_settings(now);
    if(!preferences.enabled||shutting_down||ml_shutdown_requested)fx_stop();
    if(preferences.enabled&&!attempted&&!bayer_active&&!shutting_down&&!ml_shutdown_requested&&
       now-started_ms>=1500&&!lens_info.job_state&&lv&&!lv_paused&&!get_halfshutter_pressed()&&!gui_menu_shown()){
        attempted=1;
        if(fx_start()&&bayer_camera_native())bayer_gate_automatic(1);
    }
    /* A menu OFF arriving while startup resolves the module family wins. */
    if(!preferences.enabled)fx_stop();
    fx_service();
    if(!bayer_active){bayer_gate_automatic(0);bayer_raw_trace_cancel();}
    bayer_raw_trace_service();bayer_gate_service();
    bayer_gate_report capture;bayer_gate_status(&capture);
    if(bayer_active&&capture.phase==MG_FAILED)fx_stop();
    if(dirty&&!lens_info.job_state&&capture.phase!=MG_HELD&&!get_halfshutter_pressed()&&
       (shutting_down||ml_shutdown_requested||now-changed_ms>=(save_error?2000u:400u))){
        bayer_settings snapshot=preferences;
        if(bayer_settings_save(&snapshot)){
            if(bayer_settings_equal(&preferences,&snapshot))dirty=0;
            save_error=0;
        }else{save_error=1;changed_ms=now;}
    }
}
static void worker(void*unused)
{
    while(!shutting_down||bayer_active){service_once();msleep(20);}
    /* Persist the desired ON/OFF state, never the transient shutdown state. */
    if(dirty)bayer_settings_save(&preferences);
    finished=1;
}
PROP_HANDLER(PROP_HALF_SHUTTER){if(buf&&len){unsigned down=((unsigned char*)buf)[0]!=0;fx_half(down);bayer_gate_half_press(down);}}
PROP_HANDLER(PROP_GUI_STATE){if(buf&&len>=4)fx_canon_gui(((unsigned*)buf)[0]);}
static unsigned int key(unsigned int value)
{
    if(value==MODULE_KEY_PRESS_HALFSHUTTER){fx_half(1);bayer_gate_half_press(1);}
    if(value==MODULE_KEY_PRESS_FULLSHUTTER&&bayer_camera_native())bayer_gate_full_press();
    return fx_key(value); /* Physical shutter, SET and Play remain Canon controls. */
}
static void settings_changed(void)
{
    synchronize_settings(get_ms_clock());
}
static MENU_SELECT_FUNC(toggle)
{
    preferences.enabled=!preferences.enabled;
    settings_changed();
}
static MENU_SELECT_FUNC(configure)
{
    unsigned*value=priv;unsigned low=value==&preferences.strength?1:0;
    unsigned high=value==&preferences.strength?8:value==&preferences.preset||value==&preferences.phase?3:2;
    int next=(int)*value+(delta<0?-1:1);
    *value=next<(int)low?high:next>(int)high?low:(unsigned)next;
    if(value==&preferences.preset&&*value<3)bayer_settings_preset(&preferences,*value);
    else if(value!=&preferences.preset)preferences.preset=3;
    settings_changed();
}
static MENU_SELECT_FUNC(shuffle)
{
    (void)priv;(void)delta;
    /* One action picks a stable recipe. No per-frame or per-shot randomness. */
    static uint32_t sequence;uint32_t s=get_us_clock()^(++sequence*0x9e3779b9u);
    if(!s)s=1;
    s^=s<<13;s^=s>>17;s^=s<<5;
    preferences.strength=1+s%8;preferences.pattern=(s>>4)%3;
    preferences.size=(s>>8)%3;preferences.phase=(s>>12)%4;preferences.preset=3;
    settings_changed();
}
static const char*reason(unsigned code)
{
    switch(code){case 10:return "Not enough memory";case 11:return "Stop other effect";
    case 12:return "Supported 1x LCD required";case 13:return "Image processing";
    case 14:return "Camera not ready";case 16:return "Cancelled";default:return "Capture unavailable";}
}
static const char*capture_reason(unsigned code)
{
    switch(code){
        case MG_NO_PREVIEW:return "Camera not ready";
        case MG_TRACE_BUSY:return "RAW observers busy";
        case MG_TRACE_FAILED:return "RAW observers failed";
        case MG_GUARD:return "Capture ROM mismatch";
        case MG_PATCH:return "Capture hook failed";
        case MG_CONTEXT:return "Unexpected capture context";
        case MG_NO_SHUTTER:return "Missing or expired shutter latch";
        case MG_READOUT:return "Sensor readout not ready";
        case MG_OVERLAP:return "Competing RAW reader";
        case MG_CANCEL:return "Capture cancelled";
        case MG_TIMEOUT:return "Capture timed out";
        case MG_OVERRUN:return "Processing time limit exceeded";
        case MG_LOG:return "Diagnostic log write failed";
        case MG_SHUTDOWN:return "Camera shutting down";
        case MG_SOURCE:return "Sensor source changed";
        case MG_PATCH_PREP:return "RAW processing preflight failed";
        case MG_PATCH_VERIFY:return "RAW output verification failed";
        case MG_CONTROLS:return "Unsupported bayer controls";
        default:return "Unknown capture error";
    }
}
static MENU_UPDATE_FUNC(status)
{
    fx_status r;fx_status_get(&r);bayer_gate_report capture;bayer_gate_status(&capture);
    if(capture.phase==MG_FAILED&&capture.error!=MG_CANCEL&&capture.error!=MG_SHUTDOWN){
        /* EOS M's Canon formatter prints literal 'u' for %u. Format numbers
         * with our bounded formatter and pass only strings to the core menu. */
        char value[35],help[100];
        gl_snprintf(value,sizeof(value),"Capture stopped (%u)",capture.error);
        if(capture.error==MG_TRACE_FAILED){
            bayer_raw_trace_report trace;bayer_raw_trace_status(&trace);
            gl_snprintf(help,sizeof(help),"%s. %s; trace %u, core %u, PC %08x. Restart.",
                        FX_VERSION,capture_reason(capture.error),trace.error,trace.core_error,trace.error_pc);
        }else if(capture.error==MG_OVERLAP){
            gl_snprintf(help,sizeof(help),"%s. %s at %08x. OFF/ON to retry.",
                        FX_VERSION,capture_reason(capture.error),capture.first_overlap_pc);
        }else if(capture.error==MG_PATCH){
            gl_snprintf(help,sizeof(help),"%s. %s; core %u. Restart before retry.",
                        FX_VERSION,capture_reason(capture.error),capture.core_error);
        }else gl_snprintf(help,sizeof(help),"%s. %s. OFF/ON to retry.",FX_VERSION,capture_reason(capture.error));
        MENU_SET_VALUE("%s",value);MENU_SET_HELP("%s",help);
    }else if(r.error){MENU_SET_VALUE("%s",reason(r.error));
        MENU_SET_HELP("%s. %s",FX_VERSION,bayer_camera_native()?
            "A+ / Auto ISO, Canon RAW and native 1x LCD. LiveView stays original.":
            "Use native 1x LCD and Global Draw ON. Preview only; photos stay original.");
    }else{
        MENU_SET_VALUE("%s",!r.running?(preferences.enabled?"ON - Preparing":"OFF"):capture.phase==MG_ARMED?"ON - Ready":capture.phase==MG_HELD?"ON - Saving":"ON - Preparing");
        MENU_SET_HELP("%s. LiveView stays original. Physical shutter applies Bayer Drift to RAW. Settings remembered.",FX_VERSION);
    }
    if(save_error)MENU_SET_HELP("Could not save preferences. Check card write access; settings remain active this session.");
}
static struct menu_entry entries[]={
    {.name="Bayer Drift",.priv=&preferences.enabled,.max=1,.select=toggle,.update=status,
     .help=FX_VERSION " / Intentional colour interference. EOS M 2.0.2 only.",
     .children=(struct menu_entry[]){
        {.name="Photo effect",.priv=&preferences.enabled,.max=1,.select=toggle,.update=status},
        {.name="Preset",.priv=&preferences.preset,.max=3,.select=configure,.choices=presets,
         .help="Subtle, Crunchy or Overdrive. Editing a control selects Custom."},
        {.name="Strength",.priv=&preferences.strength,.min=1,.max=8,.select=configure,
         .help="Widen local sample displacements. Higher values create stronger interference."},
        {.name="Pattern",.priv=&preferences.pattern,.max=2,.select=configure,.choices=patterns,
         .help="Dots displaces both axes; Horizontal or Vertical displaces one."},
        {.name="Texture size",.priv=&preferences.size,.max=2,.select=configure,.choices=sizes,
         .help="Fine, Medium or Coarse local sampling tiles."},
        {.name="Phase",.priv=&preferences.phase,.max=3,.select=configure,.choices=phases,
         .help="Choose which colour-sample rows and columns are displaced."},
        {.name="Shuffle",.select=shuffle,
         .help="Pick a new recipe. It stays fixed until you change it."},
        MENU_EOL,
     }},
};
static unsigned int init(void)
{
    if(!bayer_select_camera())return 1;
    fx_camera_init();if(!fx_native_capture_mode())return 1;
    const bayer_native_backend*native=bayer_camera_native();
    if(native&&!fx_capture_scratch_configure(fx_bayer_workspace(native->width,native->pitch)))return 1;
    bayer_settings_load(&preferences);
    fx_params p=bayer_settings_params(&preferences);fx_controls(&p);
    applied_preferences=preferences;
    /* Module handlers are registered after init. Seed the current state before
     * the worker starts; an initial Canon review is not an idle LiveView. */
    fx_canon_gui((unsigned)gui_state);shutting_down=finished=0;attempted=dirty=save_error=0;started_ms=get_ms_clock();
    uintptr_t task=(uintptr_t)task_create("bayer_worker",0x1e,0x4000,worker,0);
    if(!task||task==0xffffffff)return 1;
    menu_add("Shoot",entries,COUNT(entries));return 0;
}
static unsigned int deinit(void)
{
    bayer_gate_shutdown();bayer_raw_trace_shutdown();shutting_down=1;fx_stop();
    bayer_gate_report capture;
    for(unsigned i=0;i<600;i++){
        bayer_gate_status(&capture);
        if(finished&&capture.phase!=MG_HELD)break;
        msleep(5);
    }
    bayer_gate_status(&capture);if(!finished||capture.phase==MG_HELD)return 1;
    menu_remove("Shoot",entries,COUNT(entries));return 0;
}
MODULE_INFO_START() MODULE_LONGNAME("Bayer Drift") MODULE_INIT(init) MODULE_DEINIT(deinit) MODULE_INFO_END()
MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS,key,0)
MODULE_CBRS_END()
MODULE_PROPHANDLERS_START()
    MODULE_PROPHANDLER(PROP_HALF_SHUTTER) MODULE_PROPHANDLER(PROP_GUI_STATE)
MODULE_PROPHANDLERS_END()
MODULE_CONFIGS_START() MODULE_CONFIG(bayer_active) MODULE_CONFIGS_END()
