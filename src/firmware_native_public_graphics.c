/* Atomic public C6502 drawing contracts. No guest execution or guest IRQ
 * callbacks occur in this module. Internal ROM entry points remain separate.
 * The public void-call ABI preserves caller stack/banks, not dead scratch
 * flags or every intermediate firmware RAM store. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#include "firmware_native_graphics_abi.h"
#define PG_POINTER unsigned long
#else
#define PG_POINTER uintptr_t
#endif
#include "firmware_native_graphics_io.h"
#define PG_INLINE static inline __attribute__((always_inline))
typedef struct { s6502_iram_asm_context_t *c; firmware_native_graphics_services_t *g; uint8_t *m; uint8_t **pages; uint32_t changed; } pg_env;
PG_INLINE uint8_t pg_read(pg_env *e,uint16_t a)
{
    uint8_t *p=e->pages[a>>8];
    return p?p[a&255u]:((uint8_t(*)(uint16_t))(PG_POINTER)e->c->read8)(a);
}
PG_INLINE uint16_t pg_word(pg_env *e,uint16_t a)
{ return (uint16_t)(pg_read(e,a)|((uint16_t)pg_read(e,(uint16_t)(a+1u))<<8)); }
PG_INLINE uint8_t pg_rom(pg_env *e,uint32_t a)
{ return ((uint8_t(*)(uint32_t))(PG_POINTER)e->g->read_physical)(a); }
PG_INLINE int pg_source(pg_env *e,uint32_t a,uint32_t n)
{
    uint32_t p;
    if(!n || a<0x400u || a+n>0x10000u)return 0;
    for(p=a>>8;p<=(a+n-1u)>>8;++p) {
        PG_POINTER base=(PG_POINTER)e->pages[p];
        if(!base || (base<(PG_POINTER)e->m+0x1100u && base+256u>(PG_POINTER)e->m+0x400u))return 0;
    }
    return 1;
}
PG_INLINE void pg_put(pg_env *e,uint32_t column,uint32_t y,uint8_t value,uint8_t mask)
{
    uint32_t a=fw_gfx_lcd_address(column,y);
    uint8_t old=e->m[a],next=(uint8_t)((old&~mask)|(value&mask));
    if(next!=old) { e->m[a]=next;e->changed=1; }
}
/* Source spans have already been validated. Resolve once per page, not byte. */
PG_INLINE void pg_copy(pg_env *e,uint32_t source,uint8_t *out,uint32_t n)
{
    while(n) {
        uint32_t count=256u-(source&255u),i;
        const uint8_t *p=e->pages[source>>8]+(source&255u);
        if(count>n)count=n;
        for(i=0;i<count;++i)out[i]=p[i];
        source+=count;out+=count;n-=count;
    }
}
PG_INLINE void pg_row(pg_env *e,uint32_t x,uint32_t y,uint32_t width,const uint8_t *row)
{
    uint32_t first=x>>3,last=(x+width-1u)>>3,shift=x&7u,stride=(width+7u)>>3,col;
    for(col=first;col<=last;++col) {
        uint32_t i=col-first;uint8_t bits=i<stride?(uint8_t)(row[i]>>shift):0,mask=255;
        if(i && shift)bits|=(uint8_t)(row[i-1u]<<(8u-shift));
        if(col==first)mask&=(uint8_t)(255u>>shift);
        if(col==last)mask&=(uint8_t)(255u<<(7u-((x+width-1u)&7u)));
        pg_put(e,col,y,bits,mask);
    }
}
PG_INLINE uint32_t pg_chinese_offset(pg_env *e,uint8_t *low,uint8_t *high)
{
    uint32_t l=*low,h=*high,index,i,mapped=0;
    if(h<0xa1u || (h>=0xaau && l<0xa1u)) {
        for(i=0;i<228u;++i)if(pg_rom(e,0xeb7a93u+i)==h && pg_rom(e,0xeb7b77u+i)==l)break;
        if(i==228u){l=h=0xa1u;}
        else {l=pg_rom(e,0xeb7d3fu+i);h=pg_rom(e,0xeb7c5bu+i);mapped=1;}
    }
    *low=(uint8_t)l;*high=(uint8_t)h;
    if(mapped && h<0xaau)return 0x47800u+((h-0xa1u)*97u+(uint8_t)(l-0x40u))*32u;
    if(h<0xaau && l<0xa1u) {
        if(h<0xa8u)return 0x47800u+((h-0xa1u)*97u+(uint8_t)(l-0x40u))*32u;
        index=(h-0xa8u)*96u+(uint8_t)(l-(l>=0x80u?0x41u:0x40u));
        return 0x3bd80u+index*32u;
    }
    index=(uint8_t)(l-0xa1u);
    if(h<0xaau)return 0x34e00u+((h-0xa1u)*94u+index)*32u;
    if(h<0xb0u)return 0x3df40u+((h-0xaau)*94u+index)*32u;
    if(h<0xf8u)return ((h-0xb0u)*94u+index)*32u;
    return 0x425c0u+((h-0xf8u)*94u+index)*32u;
}
__attribute__((used,noinline,section(".text.firmware_native_public_graphics")))
uint32_t firmware_native_public_graphics(s6502_iram_asm_context_t *c)
{
    pg_env e;uint32_t pc=c->pc,x,y,x1,y1,source,flag=0,w,h,stride,i,j,kind,start=0;
    uint32_t *metrics;uint16_t args;uint8_t row[20],glyph[32];
    if(pc!=0x682du && pc!=0x5000u && pc!=0x63d7u && pc!=0x5c57u)return 0;
    if(!c->ram || !c->graphics || !c->pages || !c->read8 || (c->status&8u) ||
       c->cycles>c->cycle_budget || c->cycle_budget-c->cycles<6u)return 0;
    e.c=c;e.changed=0;e.m=(uint8_t*)(PG_POINTER)c->ram;e.pages=(uint8_t**)(PG_POINTER)c->pages;
    e.g=(firmware_native_graphics_services_t*)(PG_POINTER)c->graphics;
    if(e.g->version!=FW_GRAPHICS_SERVICE_VERSION || !e.g->page3 || !e.g->read_physical)return 0;
    metrics=(uint32_t*)(PG_POINTER)e.g->public_metrics;
    {
        uint8_t *p=(uint8_t*)(PG_POINTER)e.g->page3;
        if(e.g->framebuffer || p[0xe5]!=1 || p[0xe6] || p[0xe7]!=4 || p[0xe8] || p[0xe9]!=16)goto fallback;
        for(i=4;i<=16;++i)if(e.pages[i]!=e.m+(i<<8))goto fallback;
        if(e.pages[0x20]!=e.m+0x2000)goto fallback;
    }
    args=(uint16_t)(e.m[0x28]|((uint16_t)e.m[0x29]<<8));
    if(!pg_source(&e,args,7u))goto fallback;
    x=(uint8_t)c->ac;y=pg_read(&e,args);
    kind=pc==0x682du?1u:pc==0x5000u?2u:pc==0x63d7u?3u:4u;
    if(e.g->clock)start=((uint32_t(*)(void))(PG_POINTER)e.g->clock)();
    if(kind==1u) {
        uint32_t t;
        x1=pg_read(&e,args+1u);y1=pg_read(&e,args+2u);source=pg_word(&e,args+3u);flag=pg_read(&e,args+5u);
        if(x1>=160u || y1>=96u)goto complete;
        if(x1<x){t=x;x=x1;x1=t;}if(y1<y){t=y;y=y1;y1=t;}
        if(x1>=160u || y1>=96u)goto fallback;
        w=x1-x+1u;h=y1-y+1u;stride=flag==1u?(x1>>3)-(x>>3)+1u:(w+7u)>>3;
        if(!pg_source(&e,source,stride*h))goto fallback;
        for(j=0;j<h;++j) {
            pg_copy(&e,source,row,stride);source+=stride;
            if(flag==1u)for(i=0;i<stride;++i)pg_put(&e,(x>>3)+i,y+j,row[i],255);
            else if((x>>3)==(x1>>3)) {
                uint32_t a=fw_gfx_lcd_address(x>>3,y+j);
                uint8_t mask=(uint8_t)((255u<<(8u-(x&7u)))|(127u>>(x1&7u)));
                uint8_t value=(uint8_t)((e.m[a]&mask)|(row[0]>>(x&7u)));
                pg_put(&e,x>>3,y+j,value,255);
            } else pg_row(&e,x,y+j,w,row);
        }
        e.m[0x2081]=(uint8_t)x;e.m[0x2082]=(uint8_t)(y1+1u);e.m[0x2083]=(uint8_t)x1;e.m[0x2084]=(uint8_t)y1;e.m[0x20da]=0;
        if(!x && !y && x1>=158u && y1==95u && e.g->picture_complete)
            ((void(*)(PG_POINTER))(PG_POINTER)e.g->picture_complete)(c->ram);
    } else if(kind==2u) {
        uint32_t sx=pg_read(&e,args+1u),sy=pg_read(&e,args+2u),sw,sh;
        w=pg_read(&e,args+3u);h=pg_read(&e,args+4u);source=pg_word(&e,args+5u);
        if(!pg_source(&e,source,2u))goto fallback;
        sw=pg_read(&e,(uint16_t)source);sh=pg_read(&e,(uint16_t)(source+1u));
        if(!w || !h || x+w>160u || y+h>96u || sx+w>sw || sy+h>sh)goto complete;
        stride=(sw+7u)>>3;
        if(!pg_source(&e,source,2u+stride*sh))goto fallback;
        for(j=0;j<h;++j) {
            uint32_t base=source+2u+(sy+j)*stride+(sx>>3),bits=sx&7u;
            for(i=0;i<((w+7u)>>3);++i) {
                uint32_t b=pg_read(&e,(uint16_t)(base+i))<<8;
                if(bits && (sx>>3)+i+1u<stride)b|=pg_read(&e,(uint16_t)(base+i+1u));
                row[i]=(uint8_t)(b>>(8u-bits));
            }
            pg_row(&e,x,y+j,w,row);
        }
    } else {
        uint32_t offset,address;uint8_t low=pg_read(&e,args+1u),high=kind==4u?pg_read(&e,args+2u):0;
        w=kind==4u?16u:8u;
        if(x+w>160u || y>80u)goto complete;
        offset=kind==3u?0x780b7u+(uint32_t)low*16u:pg_chinese_offset(&e,&low,&high);
        address=(offset&65535u)|((((offset>>16)+e.m[0x3d8])&255u)<<16);
        if(address<0x800000u || address+(w<<1)>0xa00000u)goto fallback;
        for(i=0;i<(w<<1);++i)glyph[i]=pg_rom(&e,address+i);
        for(j=0;j<16u;++j)pg_row(&e,x,y+j,x+w>159u?159u-x:w,glyph+j*(w>>3));
        for(i=0;i<(w<<1);++i)e.m[0x208du+i]=glyph[i];
        e.m[0x2081]=(uint8_t)x;e.m[0x2082]=(uint8_t)(y+16u);e.m[0x208b]=(uint8_t)(x&7u);
        e.m[0x20ad]=low;e.m[0x20ae]=high;e.m[0x2f]=0x8d;e.m[0x30]=0x20;
    }
complete:
    if(e.changed && c->dirty)*(int*)(PG_POINTER)c->dirty=1;
    if(metrics) {
        ++metrics[0];++metrics[kind];
        if(e.g->clock){uint32_t elapsed=((uint32_t(*)(void))(PG_POINTER)e.g->clock)()-start;metrics[5]+=elapsed;if(elapsed>metrics[6])metrics[6]=elapsed;}
    }
    /* One atomic native service plus RTS. Guest-instruction FPS is no longer
     * a comparison of original firmware work; report API calls/host time. */
    c->pc=(uint16_t)((e.m[0x100u|(uint8_t)(c->sp+1u)]|((uint16_t)e.m[0x100u|(uint8_t)(c->sp+2u)]<<8))+1u);
    c->sp=(uint8_t)(c->sp+2u);c->ac=c->ix=c->iy=0;c->status=(c->status&~0xc3u)|3u;c->cycles+=6u;
    return 6u;
fallback:
    if(metrics)++metrics[7];return 0;
}
