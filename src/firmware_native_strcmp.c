#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define SC_POINTER unsigned long
#else
#define SC_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_strcmp")))
uint32_t firmware_native_strcmp(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(SC_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(SC_POINTER)c->read8;
    unsigned args=r[0x28]|((unsigned)r[0x29]<<8),local,left,right,a,b,n=0,cost,cross[7],i,diff,hi,p=c->status,sp=(uint8_t)c->sp;
    uint16_t pc;
    if(c->pc!=0x604fu || (p&8u) || args<0x403u || args>0xffcu || c->cycles>c->cycle_budget)return 0;
    local=args-3u;left=r[args]|((unsigned)r[args+1u]<<8);right=r[args+2u]|((unsigned)r[args+3u]<<8);
    if(left<0x400u || right<0x400u)return 0;
    for(i=0;i<7u;++i)cross[i]=((local&255u)+i)>255u;
    cost=45u;
    for(;;){
        if(left+n>=0x10000u || right+n>=0x10000u ||
           (left+n>=local && left+n<args+4u) || (right+n>=local && right+n<args+4u))return 0;
        a=read((uint16_t)(left+n));b=read((uint16_t)(right+n));
        if(a!=b || !a)break;
        cost+=209u+cross[2]+3u*(cross[3]+cross[4]+cross[5]+cross[6]);++n;
        if(cost>c->cycle_budget-c->cycles)return 0;
    }
    diff=(a-b)&255u;hi=(diff&128u)?255u:0u;
    cost+=241u+(hi!=0u)+2u*(cross[2]+cross[3]+cross[4]+cross[5]+cross[6]);
    if(!a && !b)cost+=33u+cross[3]+cross[4];
    if(cost>c->cycle_budget-c->cycles)return 0;
    left+=n;right+=n;
    r[args]=(uint8_t)left;r[args+1u]=(uint8_t)(left>>8);r[args+2u]=(uint8_t)right;r[args+3u]=(uint8_t)(right>>8);
    r[local]=r[0x20]=(uint8_t)diff;r[local+1u]=r[0x21]=(uint8_t)hi;r[local+2u]=(uint8_t)a;
    r[0x23]=(uint8_t)b;r[0x26]=(uint8_t)right;r[0x27]=(uint8_t)(right>>8);
    p=(p&~0xc3u)|0x30u|(a>=b)|(((a^b)&(a^diff)&128u)?64u:0u)|(hi?128u:2u);
    r[0x100u|sp]=(uint8_t)p;r[0x100u|(uint8_t)(sp-1u)]=0x13;
    c->ac=c->iy=hi;c->status=p;
    sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
