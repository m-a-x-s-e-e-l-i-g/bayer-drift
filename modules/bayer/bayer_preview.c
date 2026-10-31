/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade).
 * Native signed UYVY preview. Preserve composition/luma; derive colour fringes
 * from displaced CFA-grid luminance differences, without an RGB roundtrip.
 * The saved RAW kernel is independent and unchanged. */
#include "fx_bayer.h"
#include <string.h>
typedef struct {
    fx_params params;unsigned width,height,pitch,magic;
    uint32_t red_pair[360],blue_row[480];unsigned char chroma[576];
} preview_plan;
typedef uint32_t pixel32 __attribute__((may_alias));
static int layout(unsigned w,unsigned h,unsigned pitch)
{return w>=8&&w<=720&&!(w&1)&&h>=2&&h<=480&&!(h&1)&&pitch>=w*2&&pitch<=4096;}
static int overlap(const void*a,size_t an,const void*b,size_t bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
int fx_bayer_ready(const void*work,size_t n,const fx_params*p,unsigned w,unsigned h,unsigned pitch)
{
    if(!work||((uintptr_t)work&3)||n<sizeof(preview_plan)||!fx_bayer_validate(p)||!layout(w,h,pitch))return 0;
    const preview_plan*m=work;
    return m->magic==0x33525942u&&m->width==w&&m->height==h&&m->pitch==pitch&&!memcmp(&m->params,p,sizeof(*p));
}
static unsigned projected(unsigned at,unsigned extent,unsigned sensor_extent,unsigned tile,unsigned phase)
{
    unsigned sensor=at*sensor_extent/extent;
    return fx_bayer_axis(sensor,0,sensor_extent,tile,phase)*extent/sensor_extent;
}
int fx_bayer_prepare(void*work,size_t n,const fx_params*p,unsigned w,unsigned h,unsigned pitch,volatile int*stop)
{
    if(!work||((uintptr_t)work&3)||n<sizeof(preview_plan)||!fx_bayer_validate(p)||!layout(w,h,pitch))return FX_INVALID;
    if(stop&&*stop)return FX_CANCEL;
    preview_plan*m=work;m->magic=0;unsigned tile=(2*p->value[0])<<p->value[2];
    for(unsigned x=0;x<w;x+=2){
        unsigned a=p->value[1]==2?x:projected(x,w,5208,tile,p->value[3]&1);
        unsigned b=p->value[1]==2?x+1:projected(x+1,w,5208,tile,p->value[3]&1);
        m->red_pair[x/2]=(2*a+1)|((2*b+1)<<16);
    }
    for(unsigned y=0;y<h;y++){
        if(stop&&*stop)return FX_CANCEL;
        m->blue_row[y]=(p->value[1]==1?y:projected(y,h,3476,tile,p->value[3]>>1))*pitch;
    }
    for(unsigned i=0;i<576;i++){
        int value=(int)i-288;if(value< -128)value=-128;if(value>127)value=127;
        m->chroma[i]=(unsigned char)(int8_t)value;
    }
    if(stop&&*stop)return FX_CANCEL;
    m->params=*p;m->width=w;m->height=h;m->pitch=pitch;m->magic=0x33525942u;return FX_OK;
}
static inline __attribute__((always_inline)) uint32_t shade(uint32_t current,uint32_t blue,const unsigned char*row,uint32_t red,const unsigned char*clip,unsigned mode)
{
    int sum=(int)((current>>8)&255)+(int)(current>>24);
    int dr=mode==2?0:(int)row[red&65535]+(int)row[red>>16]-sum;
    int db=mode==1?0:(int)((blue>>8)&255)+(int)(blue>>24)-sum;
    /* Arithmetic right shifts round negative deltas down on GCC/ARM. Both
     * differences lie in -510..510; biased UV indices stay in 576 bytes.
     * Preserve both original Y bytes exactly, so geometry never moves. */
    int du=(db>>2)-(dr>>4),dv=(dr>>2)-(db>>5);
    unsigned u=clip[((current&255)^128)+du+160];
    unsigned v=clip[(((current>>16)&255)^128)+dv+160];
    return (current&0xff00ff00u)|u|(v<<16);
}
static uint32_t load(const unsigned char*p)
{return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void store(unsigned char*p,uint32_t value)
{p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);p[2]=(unsigned char)(value>>16);p[3]=(unsigned char)(value>>24);}
#ifdef FX_BAYER_PREVIEW_TEST_HOOK
extern void FX(bayer_preview_row_hook)(unsigned,const void*);
#endif
int fx_bayer_preview_prepared(const void*source,void*destination,size_t capacity,const fx_params*p,unsigned w,unsigned h,unsigned pitch,const void*work,size_t n)
{
    size_t bytes=(size_t)pitch*h;
    if(!source||!destination||!fx_bayer_ready(work,n,p,w,h,pitch)||capacity<bytes||
       (uintptr_t)source>UINTPTR_MAX-bytes||(uintptr_t)destination>UINTPTR_MAX-bytes||
       (source!=destination&&overlap(source,bytes,destination,bytes))||overlap(work,sizeof(preview_plan),source,bytes)||
       overlap(work,sizeof(preview_plan),destination,bytes))return FX_INVALID;
    const preview_plan*m=work;const unsigned char*s=source;unsigned char*d=destination;
    int aligned=!(((uintptr_t)source|(uintptr_t)destination|pitch)&3);
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    aligned=0;
#endif
    for(unsigned y=0;y<h;y++){
        const unsigned char*row=s+(size_t)y*pitch,*blue=s+m->blue_row[y];unsigned char*out=d+(size_t)y*pitch;
        if(aligned){
            const pixel32*a=(const pixel32*)row,*b=(const pixel32*)blue;pixel32*dest=(pixel32*)out;
            if(p->value[1]==1)for(unsigned x=0;x<w/2;x++)dest[x]=shade(a[x],0,row,m->red_pair[x],m->chroma,1);
            else if(p->value[1]==2)for(unsigned x=0;x<w/2;x++)dest[x]=shade(a[x],b[x],row,0,m->chroma,2);
            else for(unsigned x=0;x<w/2;x++)dest[x]=shade(a[x],b[x],row,m->red_pair[x],m->chroma,0);
        }else for(unsigned x=0;x<w/2;x++)store(out+x*4,shade(load(row+x*4),load(blue+x*4),row,m->red_pair[x],m->chroma,p->value[1]));
        if(pitch>w*2)memcpy(out+w*2,row+w*2,pitch-w*2);
#ifdef FX_BAYER_PREVIEW_TEST_HOOK
        if(!y||y==h/2||y+1==h)FX(bayer_preview_row_hook)(y,destination);
#endif
    }
    return FX_OK;
}
int fx_bayer_preview(const void*source,void*destination,size_t capacity,const fx_params*p,unsigned w,unsigned h,unsigned pitch)
{
    uint32_t workspace[FX_BAYER_PREVIEW_WORKSPACE/4];
    int result=fx_bayer_prepare(workspace,sizeof(workspace),p,w,h,pitch,0);
    return result?result:fx_bayer_preview_prepared(source,destination,capacity,p,w,h,pitch,workspace,sizeof(workspace));
}
