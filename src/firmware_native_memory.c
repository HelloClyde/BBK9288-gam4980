/* SysMemcpy is overlap-safe in this firmware (memmove semantics). */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FN_POINTER unsigned long
#else
#define FN_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_memory")))
uint32_t firmware_native_memory(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FN_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FN_POINTER)c->read8;
    void (*write)(uint16_t,uint8_t)=(void (*)(uint16_t,uint8_t))(FN_POINTER)c->write8;
    unsigned args=r[0x28]|((unsigned)r[0x29]<<8),dst,src,count,lo,hi,cost,i;
    unsigned a=0,p=c->status,sp=c->sp,pc,back;
    if((c->pc!=0xf5bdu && c->pc!=0xf68au) || (p&8u) || args<0x400u || args>0x7ffau)return 0;
    dst=read((uint16_t)args)|((unsigned)read((uint16_t)(args+1u))<<8);
    src=read((uint16_t)(args+2u))|((unsigned)read((uint16_t)(args+3u))<<8);
    count=read((uint16_t)(args+4u))|((unsigned)read((uint16_t)(args+5u))<<8);
    if(dst<0x400u || src<0x400u || dst+count>0x8000u || src+count>0x8000u ||
       (dst<args+6u && dst+count>args))return 0;
    lo=count&255u;hi=count>>8;back=src<dst;
    if(c->pc==0xf68au){
        unsigned page,y=0,x=hi,l=0,rr=0,unequal=0,cross;
        cost=49u+((args&255u)+1u>255u)+((args&255u)+2u>255u)+
             ((args&255u)+3u>255u)+((args&255u)+5u>255u);
        if(c->cycles>c->cycle_budget)return 0;
        for(page=0;page<hi;++page){
            cost+=8u;x=hi-page-1u;
            for(y=0;y<256u;++y){
                l=read((uint16_t)(src+y));rr=read((uint16_t)(dst+y));
                cross=((src&255u)+y>255u)+((dst&255u)+y>255u);
                if(l!=rr){cost+=13u+cross;unequal=1;goto comparison_done;}
                cost+=(y==255u?17u:21u)+cross;
                if(cost>c->cycle_budget-c->cycles)return 0;
            }
            cost+=23u;src+=256u;dst+=256u;
            p=(p&~0x40u)|((dst>>8)==128u?64u:0u);
        }
        cost+=16u+((args&255u)+4u>255u);x=lo;
        for(y=0;y<lo;++y){
            l=read((uint16_t)(src+y));rr=read((uint16_t)(dst+y));
            cross=((src&255u)+y>255u)+((dst&255u)+y>255u);
            if(l!=rr){cost+=17u+cross;unequal=1;goto comparison_done;}
            cost+=23u+cross;--x;
            if(cost>c->cycle_budget-c->cycles)return 0;
        }
        cost+=5u;
comparison_done:
        cost+=unequal?(l>rr?16u:14u):13u;
        if(cost>c->cycle_budget-c->cycles)return 0;
        a=unequal?(l>rr?1u:255u):0u;
        c->status=(p&~0x83u)|(unequal?(l>rr?1u:128u):3u);
        c->ac=a;c->ix=x;c->iy=y;
        r[0x2f]=(uint8_t)dst;r[0x30]=(uint8_t)(dst>>8);
        r[0x31]=(uint8_t)src;r[0x32]=(uint8_t)(src>>8);
        sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(unsigned)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    cost=(src>>8)==(dst>>8)?(back?68u:67u):(back?60u:62u);
    cost+=((args&255u)+1u>255u)+((args&255u)+2u>255u)+
          ((args&255u)+3u>255u)+((args&255u)+5u>255u);
    if(back){
        cost+=117u+hi*(5144u+((src+lo)&255u))+22u*lo;
        cost+=2u*((args&255u)+4u>255u);
        for(i=1;i<=4u;++i)cost+=((args&255u)+i)>255u;
    }else cost+=27u+hi*(5147u+(src&255u))+22u*lo+((args&255u)+4u>255u);
    if(lo && (src&255u)+lo>256u)cost+=(src&255u)+lo-256u;
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
    if(back){
        for(i=count;i;--i){a=read((uint16_t)(src+i-1u));write((uint16_t)(dst+i-1u),(uint8_t)a);}
        if(!lo)a=255u;
        p=(p&~0x40u)|(lo==128u?64u:0u);c->iy=255u;
    }else{
        for(i=0;i<count;++i){a=read((uint16_t)(src+i));write((uint16_t)(dst+i),(uint8_t)a);}
        if(!lo)a=0;
        if(hi)p=(p&~0x40u)|(((dst>>8)+hi)==128u?64u:0u);
        dst+=hi<<8;src+=hi<<8;c->iy=lo;
    }
    r[0x2f]=(uint8_t)dst;r[0x30]=(uint8_t)(dst>>8);
    r[0x31]=(uint8_t)src;r[0x32]=(uint8_t)(src>>8);
    c->ac=a;c->ix=0;c->status=(p&~0x83u)|3u;
    sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(unsigned)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
