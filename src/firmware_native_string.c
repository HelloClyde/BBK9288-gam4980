#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FT_POINTER unsigned long
#else
#define FT_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_string")))
uint32_t firmware_native_string(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FT_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FT_POINTER)c->read8;
    unsigned args=r[0x28]|((unsigned)r[0x29]<<8),local,source,length=0,cross=0,i,cost,end,next,p=c->status,sp=(uint8_t)c->sp;
    uint16_t pc;
    if(c->pc!=0x63ddu || (p&8u) || args<0x404u || args>0xffeu)return 0;
    local=args-4u;source=r[args]|((unsigned)r[args+1u]<<8);
    if(source<0x400u || c->cycles>c->cycle_budget)return 0;
    while(source+length<0x10000u){
        if(source+length>=local && source+length<args+2u)return 0;
        if(!read((uint16_t)(source+length)))break;
        ++length;if(length>(c->cycle_budget-c->cycles)/147u)return 0;
    }
    if(source+length>=0x10000u)return 0;
    for(i=1;i<=5u;++i)cross+=((local&255u)+i)>255u;
    cost=216u+147u*length+(length+1u)*cross;
    if(cost>c->cycle_budget-c->cycles)return 0;
    end=source+length;next=(uint16_t)(end+1u);
    r[local]=(uint8_t)length;r[local+1u]=(uint8_t)(length>>8);r[local+2u]=(uint8_t)end;r[local+3u]=(uint8_t)(end>>8);
    r[args]=(uint8_t)next;r[args+1u]=(uint8_t)(next>>8);r[0x20]=(uint8_t)length;r[0x21]=(uint8_t)(length>>8);
    c->ac=c->iy=length>>8;
    p=(p&~0xc3u)|0x30u|(end==0xffffu)|(end==0x7fffu?64u:0u)|(c->ac&128u)|(c->ac?0u:2u);
    r[0x100u|sp]=(uint8_t)p;c->status=p;
    sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
