/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#include "fx_bayer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases;static uint64_t samples;
static int floor_shift(int v,unsigned n)
{return v<0?-(((-v)+(1<<(int)n)-1)/(1<<(int)n)):v/(1<<(int)n);}
static int chroma_clip(int v){return v< -128?-128:v>127?127:v;}
#ifdef BAYER_DEV1_REFERENCE
extern size_t reference_bayer_workspace(unsigned,unsigned);
extern int reference_bayer_apply(const gl_frame*,const uint32_t[4],const fx_params*,void*,size_t,volatile int*);
extern int reference_bayer_preview(const void*,void*,size_t,const fx_params*,unsigned,unsigned,unsigned);
#endif
/* Independent per-bit Canon14 reference, rather than the production word code. */
static unsigned get(const unsigned char*p,unsigned pitch,unsigned x,unsigned y)
{
    unsigned v=0;
    for(unsigned k=0;k<14;k++){
        unsigned bit=x*14+k,word=bit/16,shift=15-bit%16;
        unsigned at=y*pitch+word*2;
        unsigned w=p[at]|((unsigned)p[at+1]<<8);v=(v<<1)|((w>>shift)&1);
    }
    return v;
}
static void set(unsigned char*p,unsigned pitch,unsigned x,unsigned y,unsigned v)
{
    for(unsigned k=0;k<14;k++){
        unsigned bit=x*14+k,word=bit/16,shift=15-bit%16,at=y*pitch+word*2;
        unsigned w=p[at]|((unsigned)p[at+1]<<8);
        w=(w&~(1u<<shift))|(((v>>(13-k))&1)<<shift);p[at]=(unsigned char)w;p[at+1]=(unsigned char)(w>>8);
    }
}
static unsigned reference(unsigned at,unsigned begin,unsigned end,unsigned tile,unsigned phase)
{
    if((at-begin)%2!=phase)return at;
    unsigned group=begin;
    while(group+2*tile<=at)group+=2*tile;
    unsigned last=group+2*tile;if(last>end)last=end;
    return group+last-2-at+2*phase;
}
/* Scalar oracle for the new UYVY approximation. It deliberately avoids the
 * packed-pair operations, lookup table, staging and production coordinate map. */
static void preview_reference(const unsigned char*s,unsigned char*d,const fx_params*p,unsigned w,unsigned h,unsigned pitch)
{
    unsigned rx[720],by[480],tile=(2*p->value[0])<<p->value[2];
    for(unsigned x=0;x<w;x++)rx[x]=p->value[1]==2?x:reference(x*5208/w,0,5208,tile,p->value[3]&1)*w/5208;
    for(unsigned y=0;y<h;y++)by[y]=p->value[1]==1?y:reference(y*3476/h,0,3476,tile,p->value[3]>>1)*h/3476;
    memcpy(d,s,(size_t)pitch*h);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x+=2){
        unsigned at=y*pitch+x*2;
        int sum=s[at+1]+s[at+3];
        int dr=s[y*pitch+rx[x]*2+1]+s[y*pitch+rx[x+1]*2+1]-sum;
        int db=s[by[y]*pitch+x*2+1]+s[by[y]*pitch+x*2+3]-sum;
        int u=(int)(int8_t)s[at]+floor_shift(db,2)-floor_shift(dr,4);
        int v=(int)(int8_t)s[at+2]+floor_shift(dr,2)-floor_shift(db,5);
        d[at]=(unsigned char)(int8_t)chroma_clip(u);d[at+2]=(unsigned char)(int8_t)chroma_clip(v);
    }
}
static void run(unsigned width,unsigned height,unsigned left,unsigned top,unsigned right,unsigned bottom,
                unsigned pad,unsigned offset,const fx_params*p)
{
    unsigned pitch=width*7/4+pad;size_t n=(size_t)pitch*height;
    unsigned char*allocation=malloc(n+64),*source=malloc(n),*expected=malloc(n);
    size_t ws=fx_bayer_workspace(width,pitch);unsigned char*w=malloc(ws+64);
    assert(allocation&&source&&expected&&w);memset(allocation,0xa7,n+64);memset(w,0xb9,ws+64);
    unsigned char*image=allocation+16+offset;
    uint32_t seed=0x982324u;
    for(size_t i=0;i<n;i++){seed=seed*1664525u+1013904223u;image[i]=(unsigned char)(seed>>24);}
    memcpy(source,image,n);memcpy(expected,image,n);uint32_t r[4]={top,left,bottom,right};
    unsigned tile=(2*p->value[0])<<p->value[2];
    for(unsigned y=top;y<bottom;y++)for(unsigned x=left;x<right;x++){
        unsigned sx=p->value[1]==2?x:reference(x,left,right,tile,p->value[3]&1);
        unsigned sy=p->value[1]==1?y:reference(y,top,bottom,tile,p->value[3]>>1);
        assert(sx>=left&&sx<right&&sy>=top&&sy<bottom&&((sx^x)&1)==0&&((sy^y)&1)==0);
        set(expected,pitch,x,y,get(source,pitch,sx,sy));samples++;
    }
    gl_frame f={image,n,width,height,pitch,GL_CANON14,0,16383};volatile int stop=0;
    assert(fx_bayer_apply(&f,r,p,w+16,ws,&stop)==FX_OK);
    assert(!memcmp(image,expected,n));
#ifdef BAYER_DEV1_REFERENCE
    unsigned char*old=malloc(n);size_t old_ws=reference_bayer_workspace(width,pitch);void*old_work=malloc(old_ws);
    assert(old&&old_work);memcpy(old,source,n);gl_frame old_frame=f;old_frame.data=old;
    assert(reference_bayer_apply(&old_frame,r,p,old_work,old_ws,&stop)==FX_OK);assert(!memcmp(image,old,n));
    free(old);free(old_work);
#endif
    /* The permutation is invertible, so no sample detail or bit is lost. */
    assert(fx_bayer_apply(&f,r,p,w+16,ws,&stop)==FX_OK);assert(!memcmp(image,source,n));
    stop=1;assert(fx_bayer_apply(&f,r,p,w+16,ws,&stop)==FX_CANCEL);assert(!memcmp(image,source,n));
    stop=0;assert(fx_bayer_apply(&f,r,p,w+16,ws-1,&stop)==FX_INVALID);
    assert(fx_bayer_apply(&f,r,p,image,n,&stop)==FX_INVALID);
    assert(fx_bayer_apply(&f,r,p,w+17,ws,&stop)==FX_INVALID);
    gl_frame bad=f;bad.size=n-1;assert(fx_bayer_apply(&bad,r,p,w+16,ws,&stop)==FX_INVALID);
    uint32_t invalid[4]={top,left,bottom,right+2};
    if(right==width)assert(fx_bayer_apply(&f,invalid,p,w+16,ws,&stop)==FX_INVALID);
    invalid[3]=right;invalid[1]=left+1;assert(fx_bayer_apply(&f,invalid,p,w+16,ws,&stop)==FX_INVALID);
    fx_params wrong=*p;wrong.value[0]=0;assert(fx_bayer_apply(&f,r,&wrong,w+16,ws,&stop)==FX_INVALID);
    assert(!memcmp(image,source,n));
    for(unsigned i=0;i<16+offset;i++)assert(allocation[i]==0xa7);
    for(size_t i=n+16+offset;i<n+64;i++)assert(allocation[i]==0xa7);
    for(unsigned i=0;i<16;i++)assert(w[i]==0xb9);
    for(size_t i=ws+16;i<ws+64;i++)assert(w[i]==0xb9);
    free(allocation);free(source);free(expected);free(w);cases++;
}
static void preview(void)
{
    const unsigned w=64,h=48,pitch=136;size_t n=pitch*h;
    unsigned char*s=malloc(n),*before=malloc(n),*d=malloc(n+32);assert(s&&before&&d);
    for(size_t i=0;i<n;i++)s[i]=(unsigned char)(i*19);
    memcpy(before,s,n);
    fx_params p={{4,0,0,0}};uint32_t workspace[FX_BAYER_PREVIEW_WORKSPACE/4];volatile int stop=1;
    assert(fx_bayer_prepare(workspace,sizeof(workspace),&p,w,h,pitch,&stop)==FX_CANCEL);stop=0;
    assert(fx_bayer_prepare(workspace,sizeof(workspace),&p,w,h,pitch,&stop)==FX_OK);
    assert(fx_bayer_ready(workspace,sizeof(workspace),&p,w,h,pitch));
    unsigned char*oracle=malloc(n);assert(oracle);
    for(unsigned strength=1;strength<=8;strength++)for(unsigned pattern=0;pattern<3;pattern++)
    for(unsigned size=0;size<3;size++)for(unsigned phase=0;phase<4;phase++){
        p=(fx_params){{strength,pattern,size,phase}};memset(d,0xe4,n+32);
        assert(fx_bayer_preview(s,d+16,n,&p,w,h,pitch)==FX_OK);assert(!memcmp(s,before,n));
        preview_reference(s,oracle,&p,w,h,pitch);assert(!memcmp(d+16,oracle,n));
        memcpy(d+16,s,n);assert(fx_bayer_preview(d+16,d+16,n,&p,w,h,pitch)==FX_OK);
        assert(!memcmp(d+16,oracle,n));
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)assert(d[16+y*pitch+x*2+1]==s[y*pitch+x*2+1]);
        for(unsigned y=0;y<h;y++)assert(!memcmp(s+y*pitch+w*2,d+16+y*pitch+w*2,pitch-w*2));
        for(unsigned i=0;i<16;i++)assert(d[i]==0xe4&&d[n+16+i]==0xe4);
    }
    assert(fx_bayer_preview(s,s+1,n,&p,w,h,pitch)==FX_INVALID);
    assert(fx_bayer_preview(s,d,n-1,&p,w,h,pitch)==FX_INVALID);
    unsigned char*unaligned=malloc(n+1),*fallback=malloc(n+1);assert(unaligned&&fallback);
    memcpy(unaligned+1,s,n);assert(fx_bayer_preview(unaligned+1,fallback+1,n,&p,w,h,pitch)==FX_OK);
    assert(!memcmp(fallback+1,oracle,n));free(unaligned);free(fallback);
    p.value[2]=3;assert(!fx_bayer_validate(&p));
    free(s);free(before);free(d);free(oracle);
    n=1440*480;s=malloc(n);d=malloc(n);assert(s&&d);
    for(size_t i=0;i<n;i++)s[i]=(unsigned char)((i*37)^(i>>7));
    p=(fx_params){{4,0,0,0}};assert(fx_bayer_preview(s,d,n,&p,720,480,1440)==FX_OK);
    uint32_t hash=2166136261u;
    for(size_t i=0;i<n;i++)hash=(hash^d[i])*16777619u;
    printf("BAYER_PREVIEW_FNV=%lu\n",(unsigned long)hash);free(s);free(d);
#ifdef BAYER_DEV1_REFERENCE
    s=malloc(n);d=malloc(n);unsigned char*reference=malloc(n);assert(s&&d&&reference);
    for(size_t i=0;i<n;i++)s[i]=(unsigned char)((i*37)^(i>>7));
    for(unsigned strength=1;strength<=8;strength++)for(unsigned pattern=0;pattern<3;pattern++)
    for(unsigned size=0;size<3;size++)for(unsigned phase=0;phase<4;phase++){
        p=(fx_params){{strength,pattern,size,phase}};
        preview_reference(s,reference,&p,720,480,1440);
        assert(fx_bayer_prepare(workspace,sizeof(workspace),&p,720,480,1440,0)==FX_OK);
        assert(fx_bayer_preview_prepared(s,d,n,&p,720,480,1440,workspace,sizeof(workspace))==FX_OK);
        assert(!memcmp(d,reference,n));
        memcpy(d,s,n);assert(fx_bayer_preview_prepared(d,d,n,&p,720,480,1440,workspace,sizeof(workspace))==FX_OK);
        assert(!memcmp(d,reference,n));
    }
    free(s);free(d);free(reference);puts("BAYER_UYVY_REFERENCE: all 288 full-resolution previews match scalar oracle; RAW remains dev1-identical PASS");
#endif
}
int main(int argc,char**argv)
{
    (void)argv;
    if(argc>1){fx_params p={{8,0,2,3}};run(5280,3528,72,52,5280,3528,0,0,&p);}
    else{
        for(unsigned strength=1;strength<=8;strength++)for(unsigned pattern=0;pattern<3;pattern++)
        for(unsigned size=0;size<3;size++)for(unsigned phase=0;phase<4;phase++){
            fx_params p={{strength,pattern,size,phase}};
            run(32,18,2,2,30,18,4,0,&p);run(40,22,0,0,40,22,0,1,&p);
            run(8,2,0,0,8,2,2,0,&p);
        }
        preview();
    }
    printf("BAYER_RAW={\"cases\":%u,\"samples\":%llu,\"mismatches\":0}\n",cases,(unsigned long long)samples);
    return 0;
}
