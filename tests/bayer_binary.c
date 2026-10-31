/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic SDK on an ARM board, linked to the existing .mo without recompiling
 * its source. No Canon firmware, loader, physical keys or sensor DMA is tested.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <config.h>
#include <lens.h>
#include "fx_camera.h"
#include "bayer_camera.h"
#include "bayer_capture_gate.h"
#include "bayer_settings.h"
#undef malloc
#undef free

#define CHECK(x) do {if(!(x)){printf("BINARY FIXTURE FAIL line %u: %s\n",__LINE__,#x);exit(1);}} while(0)
#ifdef BINARY_MODULE_MANAGER
static void fixture_manager_load(void);
static unsigned fixture_manager_callback(unsigned type,unsigned value);
static void fixture_manager_property(unsigned id,uint32_t value);
#endif
#ifdef BINARY_DYNAMIC_LOADER
#include "arm_module_dynamic_symbols.h"
#else
extern module_info_t __module_info_bayer;
extern module_cbr_t __module_cbr_bayer[];
extern module_prophandler_t *__module_prophandlers_bayer[];
extern int bayer_active;
#endif
static const char *model,*firmware;
static unsigned clock_ms,allocations,guard_reads,patches,forbidden_native,menu_count,removed,writes,profiles_checked,physical_half;
static void (*pending_worker)(void*);
static struct menu_entry *entry;
static int stepping;
static jmp_buf step_end;
int lv=1,lv_paused,lv_dispsize=1,ext_monitor_hdmi,sensor_cleaning,ml_shutdown_requested,gui_state;
struct lens_info lens_info;
struct vram_info vram_lv;
static struct {unsigned size;unsigned char data[40];} files[2];

int is_camera(const char*m,const char*f){return !strcmp(model,m)&&!strcmp(firmware,f);}
int get_ms_clock(void){return (int)clock_ms;}
uint64_t get_us_clock(void){return (uint64_t)clock_ms*1000;}
bool get_halfshutter_pressed(void){return physical_half!=0;}
int gui_menu_shown(void){return 0;}
int module_display_filter_enabled(void){return 0;}
int module_get_next_loaded(int i){(void)i;return -1;}
const char *module_get_name(int i){(void)i;return NULL;}
struct config_var *module_get_config_var(const char*n){(void)n;return NULL;}
int zebra_should_run(void){CHECK(0);return 1;}
int should_draw_zoom_overlay(void){CHECK(0);return 0;}
void zoom_overlay_disable(void){CHECK(0);}
#if MODULE_MAJOR >= 8
void sync_caches(void){CHECK(0);}
#else
int menu_snprintf(char*p,int n,const char*fmt,...)
{va_list args;va_start(args,fmt);int result=vsnprintf(p,(size_t)n,fmt,args);va_end(args);return result;}
#endif
struct vram_info *get_yuv422_vram(void){CHECK(0);return &vram_lv;}
int GetFreeMemForAllocateMemory(void){return 16*1024*1024;}
int GetSizeOfMaxRegion(int *n){*n=16*1024*1024;return 0;}
void *__priv_alloc_dma_memory(size_t n){void*p=malloc(n);if(p)allocations++;return p;}
void __priv_free_dma_memory(void*p){if(p){CHECK(allocations);allocations--;free(p);}}
char *get_config_dir(void){return "ML/SETTINGS/";}
static unsigned slot(const char*p)
{CHECK(!strcmp(p,"ML/SETTINGS/bayer0.prf")||!strcmp(p,"ML/SETTINGS/bayer1.prf"));return p[strlen("ML/SETTINGS/bayer")]-'0';}
int FIO_GetFileSize(const char*p,uint32_t*n){unsigned i=slot(p);*n=files[i].size;return *n?0:-1;}
FILE *FIO_OpenFile(const char*p,unsigned f){(void)f;unsigned i=slot(p);return files[i].size?(FILE*)&files[i]:NULL;}
FILE *FIO_CreateFile(const char*p){unsigned i=slot(p);files[i].size=0;return (FILE*)&files[i];}
static unsigned handle(FILE*f){CHECK(f==(FILE*)&files[0]||f==(FILE*)&files[1]);return f==(FILE*)&files[0]?0:1;}
int FIO_ReadFile(FILE*f,void*p,size_t n){CHECK(n==40);memcpy(p,files[handle(f)].data,n);return (int)n;}
int FIO_WriteFile(FILE*f,const void*p,size_t n){CHECK(n==40);unsigned i=handle(f);memcpy(files[i].data,p,n);files[i].size=n;writes++;return (int)n;}
void FIO_CloseFile(FILE*f){(void)handle(f);}
int FIO_RemoveFile(const char*p){files[slot(p)].size=0;return 0;}
int is_dir(const char*p){(void)p;CHECK(0);return 0;}
int FIO_CreateDirectory(const char*p){(void)p;CHECK(0);return -1;}
#if MODULE_MAJOR >= 8
typedef uint32_t fixture_task;
#else
typedef struct task* fixture_task;
#endif
fixture_task task_create(const char*n,uint32_t p,uint32_t s,void*fn,void*arg)
{CHECK(!strcmp(n,"bayer_worker")&&p==0x1e&&s==0x4000&&!arg);pending_worker=fn;return (fixture_task)0x1234;}
void msleep(int ms)
{
    clock_ms+=(unsigned)ms;
    if(stepping)longjmp(step_end,1);
    if(pending_worker){pending_worker(NULL);pending_worker=NULL;}
}
void menu_add(const char*n,struct menu_entry*p,int count)
{CHECK(!strcmp(n,"Shoot")&&count==1&&!strcmp(p->name,"Bayer Drift"));entry=p;menu_count++;}
void menu_remove(const char*n,struct menu_entry*p,int count)
{CHECK(!strcmp(n,"Shoot")&&count==1&&p==entry);removed++;}
/* The synthetic core accepts known hook guards but installs no CPU hooks.
 * Every sensor/task/DMA read remains forbidden: no capture stage is simulated. */
uint32_t read_value(uint8_t*p,int instruction)
{
    const bayer_native_backend*b=bayer_camera_native();CHECK(b&&instruction);guard_reads++;
    for(unsigned i=0;i<b->point_count;i++)for(unsigned j=0;j<4;j++)if((uintptr_t)p==b->points[i].pc+j*4)return b->points[i].words[j];
    for(unsigned j=0;j<4;j++)if((uintptr_t)p==b->stage_pc+j*4)return b->stage_words[j];
    CHECK(0);return 0;
}
int patch_hook_function(uintptr_t a,uint32_t word,void*fn,const char*text)
{CHECK(bayer_camera_native()&&a&&word&&fn&&text);patches++;return 0;}
int get_current_task_id(void){forbidden_native++;CHECK(0);return 0;}
uint32_t shamem_read(uint32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
uint32_t edmac_get_base(uint32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
uint32_t edmac_get_flags(uint32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
uint32_t edmac_get_state(uint32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
uint32_t edmac_get_address(uint32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
uint32_t edmac_get_pointer(uint32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
int edmac_fix_off1(int32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}
int edmac_fix_off2(int32_t a){(void)a;forbidden_native++;CHECK(0);return 0;}

static void pump(void)
{
    CHECK(pending_worker);
    if(!setjmp(step_end)){stepping=1;pending_worker(NULL);}
    stepping=0;
}
static unsigned callback(unsigned type,unsigned value)
{
#ifdef BINARY_MODULE_MANAGER
    return fixture_manager_callback(type,value);
#else
    for(module_cbr_t*c=__module_cbr_bayer;c->name;c++)if(c->type==type)return c->handler(value);
    CHECK(0);return 0;
#endif
}
static void property(unsigned id,uint32_t value)
{
#ifdef BINARY_MODULE_MANAGER
    fixture_manager_property(id,value);
#else
    for(module_prophandler_t**p=__module_prophandlers_bayer;*p;p++)if((*p)->property==id){(*p)->handler(id,NULL,&value,4);return;}
    CHECK(0);
#endif
}
static void status(const char*expected)
{
    char value[128]={0},help[256]={0};struct menu_display_info info={0};
    info.value=value;info.help=help;entry->selected=1;entry->update(entry,&info);
    if(strcmp(value,expected))printf("Status mismatch on %s: got '%s', wanted '%s'\n",model,value,expected);
    CHECK(!strcmp(value,expected));
}
static void capture_ready(void)
{
    for(module_cbr_t*c=__module_cbr_bayer;c->name;c++)CHECK(c->type!=CBR_DISPLAY_FILTER);
    CHECK(!fx_filter(0));unsigned char data[16];memset(data,0x6d,sizeof(data));
    struct display_filter_buffers buffers={data,data+8};CHECK(!fx_filter(&buffers));
    for(unsigned i=0;i<sizeof(data);i++)CHECK(data[i]==0x6d);
    fx_state controls;CHECK(fx_delivered_state(&controls));
#ifdef BINARY_CORRUPT_OUTPUT
    controls.params.value[0]^=1; /* Deliberate captured-recipe oracle check. */
#endif
    CHECK(controls.initialized&&controls.params.value[0]==4&&!controls.params.value[1]&&!controls.params.value[2]);
    CHECK(!controls.width&&!controls.height&&!controls.frames);
    fx_status report;fx_status_get(&report);CHECK(report.running&&report.allocated==39600&&!report.frames);
    property(PROP_GUI_STATE,3);CHECK(!fx_delivered_state(&controls));
    property(PROP_GUI_STATE,0);CHECK(fx_delivered_state(&controls));
}
int main(void)
{
#ifndef BINARY_MODULE_MANAGER
    CHECK(__module_info_bayer.api_magic==0x5a&&__module_info_bayer.api_major==MODULE_MAJOR);
#endif
    const char*profiles[][2]={{"EOSM","2.0.2"}};
    for(unsigned i=0;i<sizeof(profiles)/sizeof(*profiles);i++){
#ifdef BINARY_PROFILE_INDEX
        if(i!=BINARY_PROFILE_INDEX)continue;
#endif
        profiles_checked++;
        model=profiles[i][0];firmware=profiles[i][1];clock_ms=100;
        lv=1;lv_paused=ml_shutdown_requested=0;gui_state=3;lens_info.job_state=0;
        vram_lv=(struct vram_info){NULL,720,1440,!strcmp(model,"1100D")?240:480};
        memset(files,0,sizeof(files));menu_count=removed=guard_reads=patches=0;
#ifdef BINARY_MODULE_MANAGER
        fixture_manager_load();
#else
        bayer_settings defaults={0,1,4,0,0,0};CHECK(bayer_settings_save(&defaults));writes=0;
        CHECK(!__module_info_bayer.init());
#endif
        CHECK(__module_info_bayer.api_magic==0x5a&&__module_info_bayer.api_major==MODULE_MAJOR);
        CHECK(menu_count==1&&!bayer_active&&!allocations);status("OFF");
        CHECK(!strcmp(bayer_camera_name(),model));
        int native=bayer_camera_native()!=NULL;
        CHECK(native==(MODULE_MAJOR==8&&!strcmp(model,"EOSM")));
        entry->select(entry->priv,1);clock_ms=1700;pump();CHECK(bayer_active&&allocations);status("ON - Preparing");
        fx_state unavailable;CHECK(!fx_delivered_state(&unavailable)); /* Existing Canon review has priority. */
        gui_state=0;property(PROP_GUI_STATE,0);
        pump();capture_ready();status("ON - Ready");
        CHECK(callback(CBR_KEYPRESS,MODULE_KEY_PRESS_SET)==1&&callback(CBR_KEYPRESS,MODULE_KEY_PLAY)==1);
        entry->select(entry->priv,1);clock_ms+=500;pump();CHECK(!bayer_active&&!allocations);status("OFF");
        entry->select(entry->priv,1);pump();CHECK(bayer_active&&allocations);status("ON - Ready");
        capture_ready();
        CHECK(!__module_info_bayer.deinit()&&removed==1&&!bayer_active&&!allocations&&writes);
        if(!native)CHECK(!guard_reads&&!patches);
        CHECK(!forbidden_native);
        printf("BINARY_PROFILE %s %s %u %u PASS\n",model,firmware,MODULE_MAJOR,vram_lv.height);
    }
    model="EOSM";firmware="0.0.0";menu_count=0;
    CHECK(__module_info_bayer.init()==1&&!menu_count&&!allocations);
    printf("BINARY_MODULE_RESULT api=%u profiles=%u unsupported=1 sensor_reads=0 PASS\n",MODULE_MAJOR,profiles_checked);
    return 0;
}
