/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#include <string.h>
#ifdef FX_SETTINGS_HOST_TEST
#include "mirror_settings_sdk.h"
#else
#include <module.h>
#include <dryos.h>
#include <config.h>
#endif
#include "bayer_settings.h"
#include "gl_format.h"
static unsigned generation,slot=1;
static uint32_t hash(const unsigned char*p)
{uint32_t h=2166136261u;for(unsigned i=0;i<36;i++)h=(h^p[i])*16777619u;return h;}
static unsigned word(const unsigned char*p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static void put(unsigned char*p,unsigned v)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static int decode(const unsigned char*data,bayer_settings*out,unsigned*seq)
{
    if(word(data)!=0x31525942u||word(data+4)!=1||word(data+36)!=hash(data))return 0;
    bayer_settings s={word(data+12),word(data+16),word(data+20),word(data+24),word(data+28),word(data+32)};
    if(!bayer_settings_valid(&s))return 0;
    *out=s;*seq=word(data+8);return 1;
}
static void filename(char path[96],unsigned which)
{gl_snprintf(path,96,"%sbayer%u.prf",get_config_dir(),which);}
static int read_slot(unsigned which,bayer_settings*out,unsigned*seq)
{
    char path[96];unsigned char data[40];uint32_t size;filename(path,which);
    if(FIO_GetFileSize(path,&size)||size!=sizeof(data))return 0;
    FILE*f=FIO_OpenFile(path,O_RDONLY|O_SYNC);if(!f)return 0;
    int count=FIO_ReadFile(f,data,sizeof(data));FIO_CloseFile(f);
    return count==(int)sizeof(data)&&decode(data,out,seq);
}
int bayer_settings_load(bayer_settings*out)
{
    if(!out)return 0;
    bayer_settings a,b;unsigned ga=0,gb=0;int va=read_slot(0,&a,&ga),vb=read_slot(1,&b,&gb);
    generation=0;slot=1;if(!va&&!vb)return 0;
    if(vb&&(!va||(int32_t)(gb-ga)>0)){*out=b;generation=gb;slot=1;}else{*out=a;generation=ga;slot=0;}
    return 1;
}
int bayer_settings_save(const bayer_settings*s)
{
    if(!s||!bayer_settings_valid(s))return 0;
    unsigned next=generation+1,target=slot^1u;unsigned char data[40];char path[96];
    put(data,0x31525942u);put(data+4,1);put(data+8,next);put(data+12,s->enabled);put(data+16,s->preset);
    put(data+20,s->strength);put(data+24,s->pattern);put(data+28,s->size);put(data+32,s->phase);put(data+36,hash(data));
    filename(path,target);FILE*f=FIO_CreateFile(path);if(!f)return 0;
    int count=FIO_WriteFile(f,data,sizeof(data));FIO_CloseFile(f);
    bayer_settings check;unsigned seq;
    if(count!=(int)sizeof(data)||!read_slot(target,&check,&seq)||seq!=next||!bayer_settings_equal(s,&check))return 0;
    generation=next;slot=target;return 1;
}
