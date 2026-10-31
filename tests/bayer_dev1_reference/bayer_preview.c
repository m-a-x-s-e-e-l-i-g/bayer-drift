/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#include "fx_bayer.h"
#include <string.h>
typedef struct {fx_params params;unsigned width,height,pitch,magic;} preview_plan;
static int layout(unsigned w,unsigned h,unsigned pitch)
{return w>=8&&w<=720&&!(w&1)&&h>=2&&h<=480&&!(h&1)&&pitch>=w*2&&pitch<=4096;}
int fx_bayer_ready(const void*work,size_t n,const fx_params*p,unsigned w,unsigned h,unsigned pitch)
{
    if(!work||((uintptr_t)work&3)||n<sizeof(preview_plan)||!fx_bayer_validate(p)||!layout(w,h,pitch))return 0;
    const preview_plan*m=work;
    return m->magic==0x31525942u&&m->width==w&&m->height==h&&m->pitch==pitch&&!memcmp(&m->params,p,sizeof(*p));
}
int fx_bayer_prepare(void*work,size_t n,const fx_params*p,unsigned w,unsigned h,unsigned pitch,volatile int*stop)
{
    if(!work||((uintptr_t)work&3)||n<sizeof(preview_plan)||!fx_bayer_validate(p)||!layout(w,h,pitch))return FX_INVALID;
    if(stop&&*stop)return FX_CANCEL;
    preview_plan*m=work;m->params=*p;m->width=w;m->height=h;m->pitch=pitch;m->magic=0x31525942u;return FX_OK;
}
static int clipped(int v){return v<0?0:v>255?255:v;}
static int signed_clip(int v){return v< -128?-128:v>127?127:v;}
static void rgb(const unsigned char*s,unsigned pitch,unsigned x,unsigned y,int out[3])
{
    const unsigned char*p=s+(size_t)y*pitch+(x&~1u)*2;
    int u=(int)(int8_t)p[0],v=(int)(int8_t)p[2],l=p[x&1?3:1];
    out[0]=clipped(l+(359*v)/256);out[1]=clipped(l-(88*u+183*v)/256);out[2]=clipped(l+(454*u)/256);
}
static unsigned preview_axis(unsigned at,unsigned extent,unsigned sensor_extent,unsigned tile,unsigned phase)
{
    unsigned sensor=at*sensor_extent/extent;
    unsigned mapped=fx_bayer_axis(sensor,0,sensor_extent,tile,phase);
    unsigned result=mapped*extent/sensor_extent;
    return result<extent?result:extent-1;
}
static void colour(const unsigned char*s,unsigned pitch,unsigned x,unsigned y,unsigned rx,unsigned by,int out[3])
{
    int red[3],blue[3],green[3];
    rgb(s,pitch,rx,y,red);rgb(s,pitch,x,by,blue);rgb(s,pitch,x,y,green);
    /* UYVY has already been demosaiced. This is a deliberately approximate
     * viewfinder: sample separate channels at displaced sensor-grid positions.
     * The photographed RAW uses exact sample permutations, not RGB math. */
    out[0]=red[0];out[1]=green[1];out[2]=blue[2];
}
int fx_bayer_preview(const void*source,void*destination,size_t capacity,const fx_params*p,unsigned w,unsigned h,unsigned pitch)
{
    size_t bytes=(size_t)pitch*h;
    if(!source||!destination||!fx_bayer_validate(p)||!layout(w,h,pitch)||capacity<bytes||
       (uintptr_t)source>UINTPTR_MAX-bytes||(uintptr_t)destination>UINTPTR_MAX-bytes)return FX_INVALID;
    uintptr_t a=(uintptr_t)source,b=(uintptr_t)destination;
    if(a<=b?b-a<bytes:a-b<bytes)return FX_INVALID;
    const unsigned char*s=source;unsigned char*d=destination;memcpy(d,s,bytes);
    uint16_t xmap[720],ymap[480];unsigned tile=(2*p->value[0])<<p->value[2];
    for(unsigned x=0;x<w;x++)xmap[x]=(uint16_t)(p->value[1]==2?x:preview_axis(x,w,5208,tile,p->value[3]&1));
    for(unsigned y=0;y<h;y++)ymap[y]=(uint16_t)(p->value[1]==1?y:preview_axis(y,h,3476,tile,p->value[3]>>1));
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x+=2){
        int c[2][3],l[2],u[2],v[2];
        for(unsigned k=0;k<2;k++){
            colour(s,pitch,x+k,y,xmap[x+k],ymap[y],c[k]);
            int r=c[k][0],g=c[k][1],b=c[k][2];
            l[k]=(77*r+150*g+29*b+128)/256;
            u[k]=(-43*r-85*g+128*b)/256;v[k]=(128*r-107*g-21*b)/256;
        }
        unsigned char*out=d+(size_t)y*pitch+x*2;
        out[0]=(unsigned char)(int8_t)signed_clip((u[0]+u[1])/2);out[1]=(unsigned char)l[0];
        out[2]=(unsigned char)(int8_t)signed_clip((v[0]+v[1])/2);out[3]=(unsigned char)l[1];
    }
    return FX_OK;
}
