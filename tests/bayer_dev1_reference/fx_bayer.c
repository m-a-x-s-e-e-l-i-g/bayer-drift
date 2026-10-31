/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade).
 * Canon14 packing primitives follow fx_mirror_registered.c. */
#include "fx_bayer.h"
#include <string.h>
#ifdef FX_BAYER_TEST_HOOK
extern void FX(bayer_plan_hook)(void);
extern void FX(bayer_write_hook)(void);
#endif
int fx_bayer_validate(const fx_params*p)
{
    if(!p||p->value[0]<1||p->value[0]>8||p->value[1]>2||p->value[2]>2||p->value[3]>3)return 0;
    for(unsigned i=4;i<8;i++)if(p->value[i])return 0;
    return 1;
}
unsigned fx_bayer_axis(unsigned at,unsigned begin,unsigned end,unsigned tile,unsigned phase)
{
    unsigned relative=at-begin;
    if((relative&1)!=(phase&1))return at;
    unsigned index=relative/2,count=(end-begin)/2,start=index/tile*tile;
    unsigned length=count-start;if(length>tile)length=tile;
    return begin+2*(start+length-1-(index-start))+(relative&1);
}
void fx_bayer_point(const fx_params*p,const uint32_t r[4],unsigned x,unsigned y,unsigned out[2])
{
    unsigned tile=(2*p->value[0])<<p->value[2];
    out[0]=p->value[1]==2?x:fx_bayer_axis(x,r[1],r[3],tile,p->value[3]&1);
    out[1]=p->value[1]==1?y:fx_bayer_axis(y,r[0],r[2],tile,p->value[3]>>1);
}
size_t fx_bayer_workspace(unsigned width,unsigned pitch)
{
    if(width<8||width>5280||(width&7)||pitch<width*7/4||pitch>16384)return 0;
    return (size_t)pitch*2+(size_t)width*4;
}
static uintptr_t address(const void*p)
{
#if defined(__arm__) || defined(FX_TEST_CANON_ALIASES)
    return (uintptr_t)p&~(uintptr_t)0x40000000u;
#else
    return (uintptr_t)p;
#endif
}
static int span(const void*p,size_t n)
{return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n&&address(p)<=UINTPTR_MAX-n;}
static int overlaps(const void*a,size_t an,const void*b,size_t bn)
{uintptr_t x=address(a),y=address(b);return x<=y?y-x<an:x-y<bn;}
static unsigned word(const unsigned char*p){return p[0]|((unsigned)p[1]<<8);}
static void put(unsigned char*p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static unsigned read14(const unsigned char*row,unsigned x)
{
    unsigned bit=x*14,shift=bit&15;row+=(bit/16)*2;unsigned a=word(row);
    return shift<=2?(a>>(2-shift))&16383:((a<<(shift-2))|(word(row+2)>>(18-shift)))&16383;
}
static void write14(unsigned char*row,unsigned x,unsigned v)
{
    unsigned bit=x*14,shift=bit&15;row+=(bit/16)*2;unsigned a=word(row);
    if(shift<=2){unsigned at=2-shift;put(row,(a&~(16383u<<at))|(v<<at));}
    else{unsigned low=16-shift,mask=(1u<<low)-1;put(row,(a&~mask)|(v>>(14-low)));
        unsigned b=word(row+2),at=low+2;mask=(1u<<(14-low))-1;put(row+2,(b&~(mask<<at))|((v&mask)<<at));}
}
static void pack8(unsigned char*d,const uint16_t*v)
{
    put(d,(v[0]<<2)|(v[1]>>12));put(d+2,(v[1]<<4)|(v[2]>>10));
    put(d+4,(v[2]<<6)|(v[3]>>8));put(d+6,(v[3]<<8)|(v[4]>>6));
    put(d+8,(v[4]<<10)|(v[5]>>4));put(d+10,(v[5]<<12)|(v[6]>>2));
    put(d+12,(v[6]<<14)|v[7]);
}
static void row(unsigned char*d,const unsigned char*s,uint16_t*decoded,const uint16_t*map,const uint32_t*r,const fx_params*p)
{
    unsigned left=r[1],right=r[3];
    if(p->value[1]==2&&!(left&7)&&!(right&7)){
        memcpy(d+left*7/4,s+left*7/4,(right-left)*7/4);return;
    }
    for(unsigned x=left;x<right;x++)decoded[x]=(uint16_t)read14(s,x);
    unsigned x=left;
    for(;x<right&&(x&7);x++)write14(d,x,decoded[map[x]]);
    for(;x+8<=right;x+=8){
        uint16_t values[8];
        for(unsigned j=0;j<8;j++)values[j]=decoded[map[x+j]];
        pack8(d+x*7/4,values);
    }
    for(;x<right;x++)write14(d,x,decoded[map[x]]);
}
int fx_bayer_apply(const gl_frame*f,const uint32_t region[4],const fx_params*params,void*work,size_t size,volatile int*stop)
{
    if(!f||!region||!params)return FX_INVALID;
    gl_frame frame=*f;fx_params p=*params;uint32_t r[4];memcpy(r,region,sizeof(r));
    size_t required=fx_bayer_workspace(frame.width,frame.pitch);
    if(!required||!fx_bayer_validate(&p)||frame.format!=GL_CANON14||!frame.height||frame.height>3528||
       (uint64_t)frame.pitch*frame.height>frame.size||!span(frame.data,frame.size)||!span(work,size)||size<required||
       ((uintptr_t)work&1)||((frame.pitch&1)!=0)||r[0]>=r[2]||r[1]>=r[3]||r[2]>frame.height||r[3]>frame.width||
       ((r[0]|r[1]|r[2]|r[3])&1)||overlaps(work,size,frame.data,frame.size)||
       overlaps(f,sizeof(*f),frame.data,frame.size)||overlaps(params,sizeof(*params),frame.data,frame.size)||
       overlaps(region,sizeof(r),frame.data,frame.size)||overlaps(f,sizeof(*f),work,size)||
       overlaps(params,sizeof(*params),work,size)||overlaps(region,sizeof(r),work,size)||
       (stop&&(overlaps((const void*)stop,sizeof(*stop),frame.data,frame.size)||overlaps((const void*)stop,sizeof(*stop),work,size))))return FX_INVALID;
    if(stop&&*stop)return FX_CANCEL;
    unsigned char*scratch=work,*image=frame.data;
    uint16_t*decoded=(uint16_t*)(scratch+frame.pitch*2);
    uint16_t*map=decoded+frame.width;
    unsigned tile=(2*p.value[0])<<p.value[2];
    for(unsigned x=r[1];x<r[3];x++)map[x]=(uint16_t)(p.value[1]==2?x:fx_bayer_axis(x,r[1],r[3],tile,p.value[3]&1));
#ifdef FX_BAYER_TEST_HOOK
    FX(bayer_plan_hook)();
#endif
    if(stop&&*stop)return FX_CANCEL;
    /* Involutive Y pairing reads BOTH originals before publishing either row.
     * All later input is in untouched row pairs. No cancellation after writes. */
    for(unsigned y=r[0];y<r[2];y++){
        unsigned point[2];fx_bayer_point(&p,r,r[1],y,point);unsigned sy=point[1];
        if(sy<y)continue;
        memcpy(scratch,image+(size_t)y*frame.pitch,frame.pitch);
        if(sy!=y)memcpy(scratch+frame.pitch,image+(size_t)sy*frame.pitch,frame.pitch);
        row(image+(size_t)y*frame.pitch,sy==y?scratch:scratch+frame.pitch,decoded,map,r,&p);
#ifdef FX_BAYER_TEST_HOOK
        if(y==r[0])FX(bayer_write_hook)();
#endif
        if(sy!=y)row(image+(size_t)sy*frame.pitch,scratch,decoded,map,r,&p);
    }
    return FX_OK;
}
