/* Integer implementation of the ROM's float alignment and rounding contract. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FA_POINTER unsigned long
#else
#define FA_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_float_add")))
uint32_t firmware_native_float_add(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FA_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FA_POINTER)c->read8;
    unsigned sp=(uint8_t)c->sp,outer=sp,alias=c->pc!=0xde29u,subtract=c->status&1u;
    unsigned lp=r[0x20]|((unsigned)r[0x21]<<8),rp=r[0x23]|((unsigned)r[0x24]<<8),base=r[0x2a]|((unsigned)r[0x2b]<<8);
    unsigned av=0,bv=0,am,bm,ea=(uint8_t)c->ac,eb=ea,exponent,sign,discard=ea,helper=0,cost,i,x,extra=0;
    uint16_t pc;
    if(c->pc!=0xde29u && c->pc!=0xe517u && c->pc!=0xe530u)return 0;
    if(alias){if(sp<17u)return 0;sp-=2u;subtract=c->pc==0xe530u;}
    if((c->status&8u) || sp<15u || lp<0x400u || rp<0x400u || lp>0xfffcu || rp>0xfffcu || base<0x400u || base>0xff4u)return 0;
    for(i=0;i<4u;++i){av|=(unsigned)read((uint16_t)(lp+i))<<(8u*i);bv|=(unsigned)read((uint16_t)(rp+i))<<(8u*i);extra+=((lp&255u)+i>255u)+((rp&255u)+i>255u);}
    bv^=subtract<<31;am=av&0xffffffu;bm=bv&0xffffffu;
    if(!(av&0x7fffffffu)){am=bm;av=bv;exponent=(bv>>23)&255u;sign=bv>>24;helper=exponent;cost=371u;}
    else if(!(bv&0x7fffffffu)){exponent=(av>>23)&255u;sign=av>>24;helper=exponent;cost=325u;}
    else{
        unsigned n,comp=0,greater,opposite=(av^bv)>>31;
        ea=(av>>23)&255u;eb=(bv>>23)&255u;am|=0x800000u;bm|=0x800000u;discard=0;
        cost=257u;
        if(ea>eb){n=ea-eb;discard=n<=24u?(bm>>(n-1u))&1u:0;bm=n<24u?bm>>n:0;cost=260u+36u*n;}
        else if(eb>ea){n=eb-ea;discard=n<=24u?(am>>(n-1u))&1u:0;am=n<24u?am>>n:0;cost=258u+36u*n;}
        for(i=3u;i;){--i;comp+=10u;if(((am>>(8u*i))&255u)!=((bm>>(8u*i))&255u)){++comp;break;}}
        greater=am>=bm;exponent=greater?ea:eb;sign=(greater?av:bv)>>24;
        if(!opposite){
            unsigned sum=am+bm+(discard!=0u),carry=sum>0xffffffu;
            cost+=10u+(am==bm?51u:comp+(greater?21u:23u));
            helper=(sum>>16)&255u;am=sum&0xffffffu;
            cost+=54u+(discard!=0u)+24u*carry;
            if(carry){am>>=1;exponent=(exponent+1u)&255u;}
        }else if(am==bm){cost+=11u+60u;am=exponent=sign=helper=0;}
        else{
            cost+=11u+comp+(greater?62u:64u);am=greater?am-bm:bm-am;
            while(!(am&0x800000u)){am<<=1;exponent=(exponent-1u)&255u;cost+=36u;}
            cost+=10u;helper=128u;
        }
        cost+=122u;
    }
    cost+=subtract+extra+(exponent&1u)+(alias?14u:0u);
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
    if(alias){r[0x100u|outer]=0xe5;r[0x100u|(uint8_t)(outer-1u)]=subtract?0x33:0x1a;}
    x=sp-13u;
    for(i=0;i<3u;++i){r[0x101u+x+i]=(uint8_t)(am>>(8u*i));r[0x105u+x+i]=(uint8_t)(bm>>(8u*i));}
    r[0x104u+x]=(uint8_t)(av>>24);r[0x108u+x]=(uint8_t)(bv>>24);
    r[0x109u+x]=(uint8_t)ea;r[0x10au+x]=(uint8_t)eb;r[0x10bu+x]=(uint8_t)(exponent>>1);r[0x10cu+x]=(uint8_t)sign;r[0x10du+x]=(uint8_t)discard;
    r[0x100u+x]=0xe0;r[0xffu+x]=9;r[0xfeu+x]=(uint8_t)helper;
    r[base+8u]=(uint8_t)am;r[base+9u]=(uint8_t)(am>>8);r[base+10u]=(uint8_t)(((am>>16)&127u)|((exponent&1u)<<7));r[base+11u]=(uint8_t)((exponent>>1)|(sign&128u));
    r[0x20]=(uint8_t)(base+8u);r[0x21]=(uint8_t)((base+8u)>>8);
    c->ac=c->ix=sp;c->iy=0;c->status=(c->status&~0xc3u)|(sp&128u)|((sp>=128u && sp<141u)?64u:0u);
    if(alias)sp=outer;
    sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
