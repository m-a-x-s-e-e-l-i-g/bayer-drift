/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade).
 * Canon14 packing primitives follow fx_mirror_registered.c. */
#include "fx_bayer.h"
#include "fx_copy.h"
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
    size_t bytes=(size_t)pitch*2+(size_t)width*4;
    return bytes<768?768:bytes;
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
typedef uint16_t alias16 __attribute__((may_alias));
typedef uint32_t alias32 __attribute__((may_alias));
static inline __attribute__((always_inline)) void unpack8(const unsigned char*s,uint16_t*v)
{
    unsigned a,b,c,d,e,f,g;
#if defined(__arm__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(!((uintptr_t)s&1)){
        if(!((uintptr_t)s&3)){
            unsigned x=((const alias32*)s)[0],y=((const alias32*)s)[1],z=((const alias32*)s)[2];
            a=x&65535;b=x>>16;c=y&65535;d=y>>16;e=z&65535;f=z>>16;g=((const alias16*)s)[6];
        }else{
            a=((const alias16*)s)[0];
            unsigned x=((const alias32*)(s+2))[0],y=((const alias32*)(s+2))[1],z=((const alias32*)(s+2))[2];
            b=x&65535;c=x>>16;d=y&65535;e=y>>16;f=z&65535;g=z>>16;
        }
    }else
#endif
    {a=word(s);b=word(s+2);c=word(s+4);d=word(s+6);e=word(s+8);f=word(s+10);g=word(s+12);}
    v[0]=(uint16_t)(a>>2);v[1]=(uint16_t)(((a&3)<<12)|(b>>4));v[2]=(uint16_t)(((b&15)<<10)|(c>>6));
    v[3]=(uint16_t)(((c&63)<<8)|(d>>8));v[4]=(uint16_t)(((d&255)<<6)|(e>>10));
    v[5]=(uint16_t)(((e&1023)<<4)|(f>>12));v[6]=(uint16_t)(((f&4095)<<2)|(g>>14));v[7]=(uint16_t)(g&16383);
}
static inline __attribute__((always_inline)) void pack8(unsigned char*d,const uint16_t*v)
{
    unsigned a=(v[0]<<2)|(v[1]>>12),b=(v[1]<<4)|(v[2]>>10),c=(v[2]<<6)|(v[3]>>8),
        e=(v[3]<<8)|(v[4]>>6),f=(v[4]<<10)|(v[5]>>4),g=(v[5]<<12)|(v[6]>>2),h=(v[6]<<14)|v[7];
#if defined(__arm__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(!((uintptr_t)d&1)){
        if(!((uintptr_t)d&3)){
            ((alias32*)d)[0]=(a&65535)|(b<<16);((alias32*)d)[1]=(c&65535)|(e<<16);
            ((alias32*)d)[2]=(f&65535)|(g<<16);((alias16*)d)[6]=(alias16)h;
        }else{
            ((alias16*)d)[0]=(alias16)a;((alias32*)(d+2))[0]=(b&65535)|(c<<16);
            ((alias32*)(d+2))[1]=(e&65535)|(f<<16);((alias32*)(d+2))[2]=(g&65535)|(h<<16);
        }
        return;
    }
#endif
    put(d,a);put(d+2,b);put(d+4,c);put(d+6,e);put(d+8,f);put(d+10,g);put(d+12,h);
}
static void xmap(uint16_t*map,unsigned count,unsigned tile,unsigned phase)
{for(unsigned x=0;x<count;x++)map[x]=(uint16_t)fx_bayer_axis(x,0,count,tile,phase);}
#define SWAP(a,b) do{uint16_t saved=(a);(a)=(b);(b)=saved;}while(0)
static void horizontal_fast(unsigned char*row,unsigned left,unsigned right,unsigned tile,unsigned phase)
{
    /* An aligned tile reverses only four lanes in each Canon14 group. Read
     * the two partner groups before publishing either; leave other CFA lanes
     * exactly as packed. No row buffer, mapping gather or per-pixel bit IO. */
    if(tile==2){
        for(unsigned x=left;x<right;x+=8){
            uint16_t a[8];unpack8(row+x*7/4,a);
            if(phase){SWAP(a[1],a[3]);SWAP(a[5],a[7]);}
            else{SWAP(a[0],a[2]);SWAP(a[4],a[6]);}
            pack8(row+x*7/4,a);
        }
        return;
    }
    for(unsigned start=left;start<right;start+=2*tile){
        unsigned end=start+2*tile;if(end>right)end=right;
        for(unsigned lo=start,hi=end-8;lo<=hi;lo+=8,hi-=8){
            uint16_t a[8],b[8];unpack8(row+lo*7/4,a);
            if(lo==hi){
                if(phase){SWAP(a[1],a[7]);SWAP(a[3],a[5]);}
                else{SWAP(a[0],a[6]);SWAP(a[2],a[4]);}
                pack8(row+lo*7/4,a);break;
            }
            unpack8(row+hi*7/4,b);
            if(phase){SWAP(a[1],b[7]);SWAP(a[3],b[5]);SWAP(a[5],b[3]);SWAP(a[7],b[1]);}
            else{SWAP(a[0],b[6]);SWAP(a[2],b[4]);SWAP(a[4],b[2]);SWAP(a[6],b[0]);}
            pack8(row+lo*7/4,a);pack8(row+hi*7/4,b);
            if(hi<8)break;
        }
    }
}
#undef SWAP
static void horizontal(unsigned char*row,unsigned left,unsigned right,unsigned block,unsigned tile,unsigned phase,uint16_t*decoded,const uint16_t*map,const uint16_t*tail)
{
    if(!((left|right)&7)&&(tile==2||!(tile&3))){horizontal_fast(row,left,right,tile,phase);return;}
    for(unsigned start=left;start<right;start+=block){
        unsigned count=right-start;if(count>block)count=block;
        const uint16_t*m=count==block?map:tail;
        if(!(start&7)&&!(count&7)){
            for(unsigned x=0;x<count;x+=8)unpack8(row+(start+x)*7/4,decoded+x);
            for(unsigned x=0;x<count;x+=8){
                uint16_t v[8];for(unsigned j=0;j<8;j++)v[j]=decoded[m[x+j]];
                pack8(row+(start+x)*7/4,v);
            }
        }else{
            /* Partial groups preserve every neighbouring packed bit. */
            for(unsigned x=0;x<count;x++)decoded[x]=(uint16_t)read14(row,start+x);
            for(unsigned x=0;x<count;x++)if(m[x]!=x)write14(row,start+x,decoded[m[x]]);
        }
    }
}
static void copy(void*d,const void*s,size_t n)
{
    unsigned char*out=d;const unsigned char*in=s;
    if(!(((uintptr_t)out^(uintptr_t)in)&3))while(n&&((uintptr_t)out&3)){*out++=*in++;n--;}
    fx_copy_cpu(out,in,n);
}
static void notify_write(unsigned*done)
{
#ifdef FX_BAYER_TEST_HOOK
    if(!*done){*done=1;FX(bayer_write_hook)();}
#else
    (void)done;
#endif
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
    uint16_t*decoded=work,*map=decoded+128,*tail=map+128;
    unsigned tile=(2*p.value[0])<<p.value[2];
    unsigned block=2*tile;if(block&7)block*=2;
    unsigned count=(r[3]-r[1])%block;
    xmap(map,block,tile,p.value[3]&1);if(count)xmap(tail,count,tile,p.value[3]&1);
#ifdef FX_BAYER_TEST_HOOK
    FX(bayer_plan_hook)();
#endif
    if(stop&&*stop)return FX_CANCEL;
    unsigned notified=0;
    /* The independent X/Y permutations commute. Process tiny X tiles in
     * place, then swap only non-fixed Y pairs as packed spans. No frame-wide
     * decoded pass, original-row snapshots, or repacking for vertical-only. */
    if(p.value[1]!=2)for(unsigned y=r[0];y<r[2];y++){
        horizontal(image+(size_t)y*frame.pitch,r[1],r[3],block,tile,p.value[3]&1,decoded,map,tail);
        notify_write(&notified);
    }
    if(p.value[1]==1)return FX_OK;
    for(unsigned y=r[0];y<r[2];y++){
        unsigned sy=fx_bayer_axis(y,r[0],r[2],tile,p.value[3]>>1);if(sy<=y)continue;
        unsigned char*a=image+(size_t)y*frame.pitch,*b=image+(size_t)sy*frame.pitch;
        if(!((r[1]|r[3])&7)){
            size_t at=r[1]*7/4,bytes=(r[3]-r[1])*7/4;
            unsigned char*temp=scratch+((uintptr_t)(a+at)&3);
            copy(temp,a+at,bytes);copy(a+at,b+at,bytes);notify_write(&notified);copy(b+at,temp,bytes);
        }else{
            for(unsigned x=r[1];x<r[3];x++){
                unsigned first=read14(a,x),second=read14(b,x);write14(a,x,second);write14(b,x,first);
            }
            notify_write(&notified);
        }
    }
    return FX_OK;
}
