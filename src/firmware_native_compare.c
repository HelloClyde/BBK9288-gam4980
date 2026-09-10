/* C6502 comparison contracts. Output is flags plus the compiler's
 * nonzero-byte count, not a host boolean. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FC_POINTER unsigned long
#else
#define FC_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
#include "firmware_native_compare_math.h"
__attribute__((used,noinline,section(".text.firmware_native_compare")))
uint32_t firmware_native_compare(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FC_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FC_POINTER)c->read8;
    uint32_t left,right,n,i,count,cost=0,a=0,b=0,difference;
    uint8_t p=(uint8_t)c->status,sp=(uint8_t)c->sp;
    uint16_t pc;
    const uint8_t *lp=0,*rp=0;
    if((p&8u) || (c->pc!=0xd340u && c->pc!=0xd362u))return 0u;
    if(c->pc==0xd340u){left=0x20u;right=0x23u;n=2u;}
    else{
        left=r[0x20]|((uint32_t)r[0x21]<<8);
        right=r[0x23]|((uint32_t)r[0x24]<<8);n=4u;
        if(left<0x400u || right<0x400u || left>0xfffcu || right>0xfffcu)return 0u;
#if !defined(FIRMWARE_NATIVE_HOST_TEST) || defined(FW_COMPARE_DIRECT_TEST)
        /* Both operands must be resident, side-effect-free, single-page
         * spans. No callbacks execute between resolution and the reads. */
        if(c->pages && c->page_kind && (left&255u)<=252u && (right&255u)<=252u){
            const uint8_t *k=(const uint8_t *)(FC_POINTER)c->page_kind;
            uint8_t *const *pages=(uint8_t *const *)(FC_POINTER)c->pages;
            if((k[left>>8]&1u) && (k[right>>8]&1u) && pages[left>>8] && pages[right>>8]){
                lp=pages[left>>8]+(left&255u);rp=pages[right>>8]+(right&255u);
            }
        }
#endif
    }
    for(i=0;i<n;++i){
        a|=(uint32_t)(n==2u?r[left+i]:(lp?lp[i]:read((uint16_t)(left+i))))<<(8u*i);
        b|=(uint32_t)(n==2u?r[right+i]:(rp?rp[i]:read((uint16_t)(right+i))))<<(8u*i);
        if(n==4u)cost+=((left&255u)+i>255u)+((right&255u)+i>255u);
    }
    difference=a-b;
    if(n==2u)difference&=0xffffu;
    count=fw_compare_count(difference,n);
    cost+=n==2u?(count?50u+8u*count:49u):(count?92u:91u)+8u*count;
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
    p=fw_compare_status(a,b,n,p);
    r[0x100u|sp]=p;
    ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
    c->pc=(uint16_t)(pc+1u);c->sp=sp;c->status=p;c->ac=p;c->ix=count;
    if(n==4u)c->iy=3u;
    c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
