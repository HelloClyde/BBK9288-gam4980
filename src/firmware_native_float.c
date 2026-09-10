/* The firmware compares float encodings, including signed zero and NaNs,
 * lexicographically. Do not substitute the host IEEE comparison rules. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FF_POINTER unsigned long
#else
#define FF_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
__attribute__((used,noinline,section(".text.firmware_native_float")))
uint32_t firmware_native_float(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FF_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FF_POINTER)c->read8;
    unsigned lp=r[0x20]|((unsigned)r[0x21]<<8),rp=r[0x23]|((unsigned)r[0x24]<<8);
    unsigned l[4],b[4],i,cost,negative,result=0,carry=c->status&1u,y=3u,saved=(uint8_t)c->ac;
    uint8_t sp=(uint8_t)c->sp;uint16_t pc;
    if(c->pc==0xe100u || c->pc==0xe520u){
        unsigned alias=c->pc==0xe520u,original_sp=sp,base=r[0x2a]|((unsigned)r[0x2b]<<8);
        unsigned av=0,bv=0,rem,q=0,exponent=0,sign=0,helper_a=0,x,extra=0,normal;
        if(alias){if(sp<17u)return 0;sp=(uint8_t)(sp-2u);}
        if((c->status&8u) || sp<15u || lp<0x400u || rp<0x400u || lp>0xfffcu || rp>0xfffcu || base<0x400u || base>0xff4u)return 0;
        for(i=0;i<4u;++i){av|=(unsigned)read((uint16_t)(lp+i))<<(8u*i);bv|=(unsigned)read((uint16_t)(rp+i))<<(8u*i);extra+=((lp&255u)+i>255u)+((rp&255u)+i>255u);}
        rem=av;normal=(av&0x7fffffffu) && (bv&0x7fffffffu);
        cost=323u;
        if(!(bv&0x7fffffffu)){q=0xffffffu;exponent=255u;sign=255u;helper_a=255u;cost=330u;}
        else if(normal){
            unsigned round=0;
            rem=(av&0x7fffffu)|0x800000u;exponent=(((av>>23)&255u)-((bv>>23)&255u)+127u)&255u;sign=((av^bv)>>24)&128u;
            bv=(bv&0x7fffffu)|0x800000u;cost=2903u;
            for(i=0;i<25u;++i){
                unsigned take=rem>=bv;if(take)rem-=bv;
                if(i<24u){q=(q<<1)|take;cost+=56u*take;rem<<=1;}else round=take;
            }
            if(round){
                ++q;cost+=93u;
                if(q>0xffffffu){q>>=1;exponent=(exponent+1u)&255u;cost+=29u;}
            }
            if(!(q&0x800000u)){q<<=1;exponent=(exponent-1u)&255u;cost+=34u;}
            helper_a=q>>16;cost+=exponent&1u;
        }
        cost+=extra+(alias?12u:0u);
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(alias){r[0x100u|original_sp]=0xe5;r[0x100u|(uint8_t)(original_sp-1u)]=0x22;}
        x=(uint8_t)(sp-13u);
        for(i=0;i<4u;++i){r[0x101u+x+i]=(uint8_t)(rem>>(8u*i));r[0x105u+x+i]=(uint8_t)(bv>>(8u*i));}
        for(i=0;i<3u;++i)r[0x109u+x+i]=(uint8_t)(q>>(8u*i));
        r[0x10cu+x]=(uint8_t)(exponent>>1);r[0x10du+x]=(uint8_t)sign;
        r[0x100u+x]=0xe2;r[0xffu+x]=0x54;r[0xfeu+x]=(uint8_t)helper_a;
        r[base+8u]=(uint8_t)q;r[base+9u]=(uint8_t)(q>>8);r[base+10u]=(uint8_t)(((q>>16)&127u)|((exponent&1u)<<7));r[base+11u]=(uint8_t)((exponent>>1)|sign);
        r[0x20]=(uint8_t)(base+8u);r[0x21]=(uint8_t)((base+8u)>>8);
        c->ac=c->ix=sp;c->iy=1;c->status=(c->status&~0xc3u)|(normal?0x30u:0u)|(sp&128u)|((sp>=128u && sp<141u)?64u:0u);
        if(alias)sp=(uint8_t)original_sp;
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(c->pc==0xe3b7u || c->pc==0xe52cu){
        unsigned alias=c->pc==0xe52cu,original_sp=sp,base=r[0x2a]|((unsigned)r[0x2b]<<8);
        unsigned av=0,bv=0,lo,hi=0,exponent=0,sign=0,helper_a=0,x,extra=0;
        if(alias){if(sp<18u)return 0;sp=(uint8_t)(sp-2u);}
        if((c->status&8u) || sp<16u || lp<0x400u || rp<0x400u || lp>0xfffcu || rp>0xfffcu || base<0x400u || base>0xff4u)return 0;
        for(i=0;i<4u;++i){av|=(unsigned)read((uint16_t)(lp+i))<<(8u*i);bv|=(unsigned)read((uint16_t)(rp+i))<<(8u*i);extra+=((lp&255u)+i>255u)+((rp&255u)+i>255u);}
        lo=av;cost=(av&0x7fffffffu)?330u:309u;
        if((av&0x7fffffffu) && (bv&0x7fffffffu)){
            unsigned aa=(av&0x7fffffu)|0x800000u,bb=(bv&0x7fffffu)|0x800000u;
            unsigned lowprod=(aa&65535u)*(bb&65535u),mid=(aa>>16)*(bb&65535u)+(bb>>16)*(aa&65535u);
            unsigned highprod=(aa>>16)*(bb>>16)+(mid>>16),sum=lowprod+(mid<<16),bits=aa;
            highprod+=sum<lowprod;lo=sum<<9;hi=(highprod<<9)|(sum>>23);
            exponent=(((av>>23)&255u)+((bv>>23)&255u)-127u)&255u;sign=(av^bv)&0x80000000u;
            cost=1798u;while(bits){cost+=56u*(bits&1u);bits>>=1;}
            if(hi&0x1000000u){lo=(lo>>1)|((hi&1u)<<31);hi>>=1;exponent=(exponent+1u)&255u;cost+=72u;}
            helper_a=lo>>24;
            if(lo&0x80000000u){
                ++hi;helper_a=0;cost+=50u;
                if(hi&0x1000000u){
                    unsigned top=((lo>>24)>>1)|((hi&1u)<<7);
                    hi>>=1;lo=(lo&0xffffffu)|(top<<24);exponent=(exponent+1u)&255u;
                    helper_a=top;cost+=46u;
                }
            }
            bv=bb;cost+=exponent&1u;
        }
        cost+=extra+(alias?12u:0u);
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(alias){r[0x100u|original_sp]=0xe5;r[0x100u|(uint8_t)(original_sp-1u)]=0x2e;}
        x=(uint8_t)(sp-14u);
        for(i=0;i<4u;++i){r[0x101u+x+i]=(uint8_t)(lo>>(i*8u));r[0x105u+x+i]=(uint8_t)(bv>>(i*8u));r[0x109u+x+i]=(uint8_t)(hi>>(i*8u));}
        r[0x10du+x]=(uint8_t)(exponent>>1);r[0x10eu+x]=(uint8_t)(sign>>24);
        r[0x100u+x]=0xe4;r[0xffu+x]=0xe7;r[0xfeu+x]=(uint8_t)helper_a;
        r[base+8u]=(uint8_t)hi;r[base+9u]=(uint8_t)(hi>>8);
        r[base+10u]=(uint8_t)(((hi>>16)&127u)|((exponent&1u)<<7));r[base+11u]=(uint8_t)((exponent>>1)|(sign>>24));
        r[0x20]=(uint8_t)(base+8u);r[0x21]=(uint8_t)((base+8u)>>8);
        c->ac=c->ix=sp;c->iy=1;c->status=(c->status&~0xc3u)|(sp&128u)|((sp>=128u && sp<142u)?64u:0u);
        if(alias)sp=(uint8_t)original_sp;
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(c->pc==0xe31du || c->pc==0xe528u){
        unsigned alias=c->pc==0xe528u,original_sp=sp,value=0,sign,n=0,normalized,exponent,x;
        if(alias){if(sp<7u)return 0;sp=(uint8_t)(sp-2u);}
        if((c->status&8u) || sp<5u || lp<0x400u || lp>0xfffcu || rp<0x400u || rp>0xffcu)return 0;
        for(i=0;i<4u;++i)value|=(unsigned)read((uint16_t)(lp+i))<<(i*8u);
        sign=value>>31;normalized=sign?0u-value:value;
        cost=69u;
        if(value){
            while(!(normalized&0x80000000u)){normalized<<=1;++n;}
            exponent=158u-n;cost=(sign?232u:187u)+35u*n+!(exponent&1u);
        }else exponent=0;
        for(i=1;i<4u;++i)cost+=(value?2u:1u)*(((lp&255u)+i)>255u);
        if(alias)cost+=12u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(alias){r[0x100u|original_sp]=0xe5;r[0x100u|(uint8_t)(original_sp-1u)]=0x2a;}
        if(value){
            x=(uint8_t)(sp-5u);for(i=0;i<4u;++i)r[0x101u+x+i]=(uint8_t)(normalized>>(8u*i));
            r[0x105u+x]=(uint8_t)(sign<<7);r[0x100u+x]=(uint8_t)(exponent>>1);
            r[rp]=(uint8_t)(normalized>>8);r[rp+1u]=(uint8_t)(normalized>>16);
            r[rp+2u]=(uint8_t)(((normalized>>24)&127u)|((exponent&1u)<<7));r[rp+3u]=(uint8_t)((exponent>>1)|(sign<<7));
            c->ac=sign<<7;c->ix=x;c->iy=0;
            c->status=(c->status&~0x83u)|(exponent&1u)|(sign?128u:2u);
            if(sign)c->status=(c->status&~64u)|(value==0x80000000u?64u:0u);
        }else{
            for(i=0;i<4u;++i)r[rp+i]=0;c->ac=0;c->iy=0;c->status=(c->status&~0x82u)|2u;
        }
        if(alias)sp=(uint8_t)original_sp;
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(c->pc==0xe282u || c->pc==0xe524u){
        unsigned alias=c->pc==0xe524u,original_sp=sp;
        unsigned value=0,mantissa,out=0,n=0,negative,x;
        if(alias){if(sp<10u)return 0;sp=(uint8_t)(sp-2u);}x=(uint8_t)(sp-8u);
        if((c->status&8u) || sp<8u || lp<0x400u || lp>0xfffcu || rp<0x400u || rp>0xffcu)return 0;
        for(i=0;i<4u;++i){value|=(unsigned)read((uint16_t)(lp+i))<<(8u*i);}
        negative=value>>31;mantissa=value&0xffffffu;
        cost=141u;
        if(value&0x7fffffffu){
            n=(((value>>23)&255u)-126u)&255u;if(!n)n=256u;
            mantissa=(value&0x7fffffu)|0x800000u;
            if(n<=24u)out=mantissa>>(24u-n);else if(n<56u)out=mantissa<<(n-24u);
            mantissa=n<24u?(mantissa<<n)&0xffffffu:0u;
            if(negative)out=0u-out;
            cost=(negative?222u:177u)+47u*n;
        }
        for(i=1;i<4u;++i)cost+=((lp&255u)+i)>255u;
        if(alias)cost+=12u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(alias){r[0x100u|original_sp]=0xe5;r[0x100u|(uint8_t)(original_sp-1u)]=0x26;}
        for(i=0;i<3u;++i)r[0x101u+x+i]=(uint8_t)(mantissa>>(8u*i));r[0x104u+x]=(uint8_t)(value>>24);
        for(i=0;i<4u;++i){r[0x105u+x+i]=(uint8_t)(out>>(8u*i));r[rp+i]=(uint8_t)(out>>(8u*i));}
        c->ac=c->ix=sp;c->iy=3u;c->status=(c->status&~0xc3u)|(sp&128u)|((sp>=128u && sp<136u)?64u:0u);
        if(alias)sp=(uint8_t)original_sp;
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    unsigned cmp_interface=c->pc==0xd31au;
    unsigned wrapper=c->pc==0xe51cu || cmp_interface,outer_sp=sp;
    if(wrapper){if(sp<(cmp_interface?5u:3u))return 0;sp=(uint8_t)(sp-(cmp_interface?4u:2u));}
    if((c->pc!=0xe039u && !wrapper) || !sp || lp<0x400u || rp<0x400u || lp>0xfffcu || rp>0xfffcu)return 0;
    for(i=0;i<4u;++i){l[i]=read((uint16_t)(lp+i));b[i]=read((uint16_t)(rp+i));}
    negative=l[3]>>7;
    cost=(negative?22u:23u)+((lp&255u)+3u>255u)+((rp&255u)+3u>255u);
    if((l[3]^b[3])&128u){result=negative?1u:2u;cost+=18u;}
    else{
        unsigned a=((l[3]<<1)|(l[2]>>7))&255u,bb=((b[3]<<1)|(b[2]>>7))&255u;
        cost+=45u+((lp&255u)+2u>255u)+((lp&255u)+3u>255u)+((rp&255u)+2u>255u)+((rp&255u)+3u>255u);
        saved=bb;carry=a>=bb;
        if(a!=bb){result=(a<bb)^negative?1u:2u;cost+=2u+(carry?2u:3u)+5u+14u;}
        else{
            cost+=3u+25u+((lp&255u)+2u>255u)+((rp&255u)+2u>255u);
            y=2;saved=b[2]&127u;a=l[2]&127u;bb=saved;
            while(a==bb && y){
                cost+=2u+12u;--y;a=l[y];bb=b[y];
                cost+=((lp&255u)+y>255u)+((rp&255u)+y>255u);
            }
            carry=a>=bb;
            if(a==bb){cost+=2u+5u+14u;result=0;}
            else{result=(a<bb)^negative?1u:2u;cost+=3u+(carry?2u:3u)+2u+14u+((negative&&!carry)?0u:3u);}
        }
    }
    if(wrapper)cost+=cmp_interface?(result==0u?46u:result==1u?54u:52u):12u;
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
    if(wrapper){
        unsigned entry_sp=outer_sp-(cmp_interface?2u:0u);
        r[0x100u|entry_sp]=0xe5;r[0x100u|(uint8_t)(entry_sp-1u)]=0x1e;
        if(cmp_interface){r[0x100u|outer_sp]=0xd3;r[0x100u|(uint8_t)(outer_sp-1u)]=0x1c;}
    }
    r[0x100u|sp]=(uint8_t)saved;c->ac=c->ix=result;c->iy=y;
    c->status=(c->status&~0x83u)|carry|(result?0u:2u);
    if(cmp_interface){
        c->ac=(c->status&0x0cu)|0x10u|(result==0u?2u:result==1u?128u:192u);
        c->status=c->ac|0x30u;r[0x100u|outer_sp]=(uint8_t)c->ac;
    }
    if(wrapper)sp=(uint8_t)outer_sp;
    ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
}
