#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FS_POINTER unsigned long
#else
#define FS_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_strchr")))
uint32_t firmware_native_strchr(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FS_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FS_POINTER)c->read8;
    unsigned args=r[0x28]|((unsigned)r[0x29]<<8),source,address,ch,value=0;
    unsigned cost,count,cross1,cross2,p=c->status,sp=(uint8_t)c->sp,diff=0,result;
    uint16_t pc;
    if(c->pc!=0x5fc4u || (p&8u) || args<0x400u || args>0xffdu || c->cycles>c->cycle_budget)return 0;
    source=r[args]|((unsigned)r[args+1u]<<8);ch=r[args+2u];address=source;
    if(source<0x400u)return 0;
    cross1=(args&255u)==255u;cross2=(args&255u)>=254u;
    count=((source&255u)!=0u)+((source>>8)!=0u);
    cost=36u+cross1+50u+8u*count+3u+5u;
    for(;;){
        if(address>=0x10000u || (address>=args && address<args+3u))return 0;
        value=read((uint16_t)address);diff=(value-ch)&255u;
        cost+=36u+cross1+cross2;
        if(value==ch){cost+=3u+29u+cross1;result=address;break;}
        cost+=5u;
        if(!value){cost+=33u+cross1+18u+26u+cross1;result=0;break;}
        cost+=32u+cross1+35u+cross1+5u;++address;
        if(cost>c->cycle_budget-c->cycles)return 0;
    }
    if(cost>c->cycle_budget-c->cycles)return 0;
    r[0x23]=r[0x24]=0;r[0x26]=(uint8_t)address;r[0x27]=(uint8_t)(address>>8);
    r[args]=r[0x20]=(uint8_t)result;r[args+1u]=r[0x21]=(uint8_t)(result>>8);
    r[0x100u|sp]=0x5f;r[0x100u|(uint8_t)(sp-1u)]=0xd9;
    r[0x100u|(uint8_t)(sp-2u)]=(uint8_t)((p&~0xc3u)|0x31u|((source>>8)&128u));
    c->ac=result>>8;c->iy=1;c->ix=count;
    c->status=(p&~0xc3u)|0x30u|(value>=ch)|(((value^ch)&(value^diff)&128u)?64u:0u)|(c->ac&128u)|(c->ac?0u:2u);
    sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
