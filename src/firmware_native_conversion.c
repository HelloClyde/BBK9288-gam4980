/* Complete C6502 operand-one/two long/float conversion contracts. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FV_POINTER unsigned long
#else
#define FV_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_conversion")))
uint32_t firmware_native_conversion(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FV_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FV_POINTER)c->read8;
    unsigned p1=r[0x20]|((unsigned)r[0x21]<<8),p2=r[0x23]|((unsigned)r[0x24]<<8),base=r[0x2a]|((unsigned)r[0x2b]<<8);
    unsigned extended=c->pc==0xd498u || c->pc==0xd4c0u || c->pc==0xd506u || c->pc==0xd534u || c->pc==0xdd19u || c->pc==0xdd32u || c->pc==0xdd52u || c->pc==0xdd6fu;
    unsigned ext_second=c->pc==0xd506u || c->pc==0xd534u || c->pc==0xdd52u || c->pc==0xdd6fu;
    unsigned ext_word=c->pc==0xd4c0u || c->pc==0xd534u || c->pc==0xdd32u || c->pc==0xdd6fu;
    unsigned ext_unsigned=c->pc>=0xdd00u,ext_cost=0;
    unsigned narrow=c->pc==0xd81du || c->pc==0xd825u || c->pc==0xd86cu || c->pc==0xd878u;
    unsigned narrow_word=c->pc==0xd825u || c->pc==0xd878u,outer_sp=(uint8_t)c->sp;
    unsigned sp=(uint8_t)c->sp,second=c->pc==0xd557u || c->pc==0xd88au || c->pc==0xdd8cu || ext_second || c->pc==0xd86cu || c->pc==0xd878u;
    unsigned to_integer=c->pc==0xd835u || c->pc==0xd88au || narrow,alias=c->pc==0xdd4fu || c->pc==0xdd8cu;
    unsigned source=second?p2:p1,target=base+(second?32u:16u),saved=second?p1:p2,value=0,result=0,n=0,cost,i,x,normalized=0,exponent=0,sign,p=c->status,ret;
    uint16_t pc;
    if(c->pc!=0xd4e1u && c->pc!=0xd557u && c->pc!=0xd835u && c->pc!=0xd88au && !alias && !extended && !narrow)return 0;
    if(narrow){if(sp<16u)return 0;sp-=2u;}
    if(extended)source=target;
    if((p&8u) || sp<14u || source<0x400u || source>0xfffcu || base<0x400u || target>0xffcu)return 0;
    if(extended){
        unsigned fill;
        value=second?p2:ext_word?p1:(uint8_t)c->ac;if(!ext_word)value&=255u;
        fill=!ext_unsigned && (value&(ext_word?0x8000u:0x80u));
        if(fill)value|=ext_word?0xffff0000u:0xffffff00u;
        ext_cost=ext_unsigned?(ext_word?83u:second?87u:77u):(second?(ext_word?93u:90u):(ext_word?86u:80u))+(fill!=0u);
        ext_cost+=ext_unsigned?12u:9u;
    }else for(i=0;i<4u;++i)value|=(unsigned)read((uint16_t)(source+i))<<(8u*i);
    sign=value>>31;p&=~0xc3u;
    if(to_integer){
        normalized=value&0xffffffu;cost=141u;
        if(value&0x7fffffffu){
            n=(((value>>23)&255u)-126u)&255u;if(!n)n=256u;
            normalized=(value&0x7fffffu)|0x800000u;
            if(n<=24u)result=normalized>>(24u-n);else if(n<56u)result=normalized<<(n-24u);
            normalized=n<24u?(normalized<<n)&0xffffffu:0;
            if(sign)result=0u-result;cost=(sign?222u:177u)+47u*n;
        }
        for(i=1;i<4u;++i)cost+=((source&255u)+i)>255u;
    }else{
        normalized=sign?0u-value:value;cost=69u;
        if(value){
            while(!(normalized&0x80000000u)){normalized<<=1;++n;}
            exponent=158u-n;result=(normalized>>8)&0x7fffffu;result|=exponent<<23;result|=sign<<31;
            cost=(sign?232u:187u)+35u*n+!(exponent&1u);
        }
        for(i=1;i<4u;++i)cost+=(value?2u:1u)*(((source&255u)+i)>255u);
    }
    cost+=(second?99u:80u)+(alias?3u:0u)+ext_cost;
    if(narrow)cost+=(second?(narrow_word?41u:29u):(narrow_word?34u:19u))+(narrow_word && (target&255u)==255u);
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
    if(narrow){r[0x100u|outer_sp]=0xd8;r[0x100u|(uint8_t)(outer_sp-1u)]=second?(narrow_word?0x7a:0x6e):(narrow_word?0x27:0x1f);}
    r[0x100u|sp]=(uint8_t)(saved>>8);r[0x100u|(uint8_t)(sp-1u)]=(uint8_t)saved;
    ret=second?(to_integer?0xd89du:0xd56au):(to_integer?0xd84au:0xd4f6u);
    r[0x100u|(uint8_t)(sp-2u)]=(uint8_t)(ret>>8);r[0x100u|(uint8_t)(sp-3u)]=(uint8_t)ret;
    r[0x100u|(uint8_t)(sp-4u)]=0xe5;r[0x100u|(uint8_t)(sp-5u)]=to_integer?0x26:0x2a;
    if(to_integer){
        x=sp-14u;for(i=0;i<3u;++i)r[0x101u+x+i]=(uint8_t)(normalized>>(8u*i));r[0x104u+x]=(uint8_t)(value>>24);
        for(i=0;i<4u;++i)r[0x105u+x+i]=(uint8_t)(result>>(8u*i));
        c->ix=sp-6u;c->iy=3u;p|=((sp-6u)>=128u && (sp-6u)<136u)?64u:0u;
    }else{
        if(value){
            x=sp-11u;for(i=0;i<4u;++i)r[0x101u+x+i]=(uint8_t)(normalized>>(8u*i));
            r[0x105u+x]=(uint8_t)(sign<<7);r[0x100u+x]=(uint8_t)(exponent>>1);c->ix=x;
            p|=(exponent&1u)|(value==0x80000000u?64u:0u);
        }
        c->iy=0;
    }
    for(i=0;i<4u;++i)r[target+i]=(uint8_t)(result>>(8u*i));
    if(second){r[0x23]=(uint8_t)target;r[0x24]=(uint8_t)(target>>8);}else{r[0x20]=(uint8_t)target;r[0x21]=(uint8_t)(target>>8);}
    c->ac=saved>>8;c->status=p|(c->ac&128u)|(c->ac?0u:2u);
    if(narrow){
        if(narrow_word){c->ix=(uint8_t)result;r[second?0x23u:0x20u]=(uint8_t)result;r[second?0x24u:0x21u]=(uint8_t)(result>>8);}
        else if(second)r[0x23]=(uint8_t)result;
        if(second)r[0x100u|outer_sp]=(uint8_t)c->ac;else c->ac=narrow_word?(result>>8)&255u:result&255u;
        c->iy=narrow_word?1u:0u;c->status=p|(c->ac&128u)|(c->ac?0u:2u);sp=outer_sp;
    }
    sp=(uint8_t)(sp+1u);pc=r[0x100u|sp];sp=(uint8_t)(sp+1u);pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
