/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#include <assert.h>
#include <string.h>
#include "mirror_ui_sdk.h"
#include "mirror_settings_sdk.h"
#include "../modules/bayer/bayer.c"
static struct {unsigned size;unsigned char data[40];} files[2];
static unsigned clock_ms=100,failed_write,control_calls;
static fx_params controls;
unsigned cli(void){return 0;}
void sei(unsigned s){(void)s;}
unsigned get_ms_clock(void){return clock_ms;}
unsigned get_us_clock(void){return clock_ms*1000;}
int fx_controls(const fx_params*p){controls=*p;control_calls++;return 1;}
void fx_stop(void){}
void bayer_gate_automatic(unsigned enabled){(void)enabled;}
void bayer_raw_trace_cancel(void){}
void fx_status_get(fx_status*s){memset(s,0,sizeof(*s));}
void bayer_gate_status(bayer_gate_report*r){memset(r,0,sizeof(*r));}
void bayer_raw_trace_status(bayer_raw_trace_report*r){memset(r,0,sizeof(*r));}
const bayer_native_backend*bayer_camera_native(void){static const bayer_native_backend n={0};return &n;}
const char*bayer_camera_name(void){return "EOSM";}
const char*get_config_dir(void){return "ML/SETTINGS/";}
static unsigned index_of(const char*p)
{
    assert(!strcmp(p,"ML/SETTINGS/bayer0.prf")||!strcmp(p,"ML/SETTINGS/bayer1.prf"));
    return p[strlen("ML/SETTINGS/bayer")]-'0';
}
int FIO_GetFileSize(const char*p,uint32_t*n){*n=files[index_of(p)].size;return *n?0:-1;}
FILE*FIO_OpenFile(const char*p,unsigned flags){(void)flags;return files[index_of(p)].size?(FILE*)&files[index_of(p)]:0;}
FILE*FIO_CreateFile(const char*p){unsigned i=index_of(p);files[i].size=0;return (FILE*)&files[i];}
int FIO_ReadFile(FILE*f,void*out,size_t n)
{unsigned i=f==(FILE*)&files[0]?0:1;assert(f==(FILE*)&files[i]&&n==40);memcpy(out,files[i].data,n);return (int)n;}
int FIO_WriteFile(FILE*f,const void*data,size_t n)
{
    unsigned i=f==(FILE*)&files[0]?0:1;assert(f==(FILE*)&files[i]&&n==40);
    if(failed_write)return -1;
    memcpy(files[i].data,data,n);files[i].size=(unsigned)n;return (int)n;
}
void FIO_CloseFile(FILE*f){assert(f==(FILE*)&files[0]||f==(FILE*)&files[1]);}
int main(void)
{
    preferences=(bayer_settings){0,1,4,0,0,0};applied_preferences=preferences;
    assert(!strcmp(entries[0].name,"Bayer Drift"));
    unsigned count=0;while(entries[0].children[count].name)count++;assert(count==7);
    for(unsigned preset=0;preset<3;preset++){
        preferences.preset=preset;synchronize_settings(clock_ms++);
        assert(preferences.preset==preset&&bayer_settings_valid(&preferences));
        assert(controls.value[0]==preferences.strength);
        configure(&preferences.strength,1);assert(preferences.preset==3);
        assert(controls.value[0]==preferences.strength);
    }
    preferences.strength=8;preferences.preset=3;applied_preferences=preferences;
    configure(&preferences.strength,1);assert(preferences.strength==1);
    configure(&preferences.strength,-1);assert(preferences.strength==8);
    preferences.phase=3;configure(&preferences.phase,1);assert(!preferences.phase);
    /* Core pickboxes can bypass the menu callback. Reconcile in worker. */
    preferences.size=(preferences.size+1)%3;synchronize_settings(clock_ms++);assert(preferences.preset==3);
    preferences.preset=0;synchronize_settings(clock_ms++);assert(preferences.strength==1&&!preferences.size);
    bayer_settings valid=preferences;preferences.phase=99;synchronize_settings(clock_ms++);
    assert(bayer_settings_equal(&preferences,&valid));
    shuffle(0,0);bayer_settings shuffled=preferences;assert(shuffled.preset==3&&bayer_settings_valid(&shuffled));
    for(unsigned i=0;i<10;i++)synchronize_settings(clock_ms++);
    assert(bayer_settings_equal(&preferences,&shuffled));
    bayer_settings loaded={0};assert(!bayer_settings_load(&loaded));
    assert(bayer_settings_save(&shuffled));assert(bayer_settings_load(&loaded));assert(bayer_settings_equal(&loaded,&shuffled));
    bayer_settings newer=shuffled;newer.phase=(newer.phase+1)%4;
    assert(bayer_settings_save(&newer));assert(bayer_settings_load(&loaded));assert(bayer_settings_equal(&loaded,&newer));
    files[1].data[20]^=0x40;assert(bayer_settings_load(&loaded));assert(bayer_settings_equal(&loaded,&shuffled));
    failed_write=1;assert(!bayer_settings_save(&newer));assert(bayer_settings_load(&loaded));assert(bayer_settings_equal(&loaded,&shuffled));
    assert(control_calls>0);puts("BAYER_MENU: presets/custom, bounds, direct setters, stable shuffle, checked-slot recovery PASS");return 0;
}
