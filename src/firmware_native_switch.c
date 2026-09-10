#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FS_POINTER unsigned long
#else
#define FS_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_switch")))
uint32_t firmware_native_switch(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FS_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FS_POINTER)c->read8;
    unsigned count=(uint8_t)c->ix|((unsigned)(uint8_t)c->iy<<8),sp=(uint8_t)c->sp;
    unsigned table=r[0x26]|((unsigned)r[0x27]<<8),key=r[0x23]|((unsigned)r[0x24]<<8),fallback=r[0x20]|((unsigned)r[0x21]<<8);
    unsigned index,cost=62u,lowdiff=0,match=0,pointer,target,remaining,p=c->status;
    if(c->pc!=0xdb5cu || (p&8u) || sp<5u || !count || count>256u || table<0x400u || table+4u*count>0x10000u)return 0;
    for(index=0;index<count;++index){
        unsigned address=table+2u*index,value=read((uint16_t)address)|((unsigned)read((uint16_t)(address+1u))<<8);
        cost+=(address&255u)==255u;lowdiff=(uint8_t)(value-key);
        if(value==key){match=1;break;}
    }
    pointer=table+2u*count+2u*index;
    if(match){
        cost+=100u*index+69u+((pointer&255u)==255u);
        target=read((uint16_t)pointer)|((unsigned)read((uint16_t)(pointer+1u))<<8);
    }else{cost+=100u*count+30u;target=fallback;}
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
    remaining=count-index;
    r[0x100u|sp]=(uint8_t)lowdiff;r[0x100u|(sp-1u)]=(uint8_t)(fallback>>8);r[0x100u|(sp-2u)]=(uint8_t)fallback;
    r[0x100u|(sp-3u)]=(uint8_t)(remaining>>8);r[0x100u|(sp-4u)]=(uint8_t)remaining;
    r[0x20]=(uint8_t)pointer;r[0x21]=(uint8_t)(pointer>>8);r[0x26]=(uint8_t)target;r[0x27]=(uint8_t)(target>>8);
    c->ac=match?target>>8:lowdiff;c->ix=match?sp:sp-5u;c->iy=1u;
    p=(p&~0xc3u)|(match?((sp>=128u && sp<133u)?64u:0u):1u);
    c->status=p|(c->ac&128u)|(c->ac?0u:2u);c->pc=target;c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
