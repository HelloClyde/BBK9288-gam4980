/* Unsigned low-16 product. Arithmetic is native; flags are reconstructed
 * from the last significant addition rather than replaying 8/16 iterations. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FM_POINTER unsigned long
#else
#define FM_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
#include "firmware_native_multiply_math.h"
__attribute__((used,noinline,section(".text.firmware_native_multiply")))
uint32_t firmware_native_multiply(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FM_POINTER)c->ram;
    uint32_t a=r[0x20]|((uint32_t)r[0x21]<<8);
    uint32_t b=r[0x23]|((uint32_t)r[0x24]<<8), bits=b, cost, last=b;
    uint32_t product=a*b, before, sum;
    uint8_t p=(uint8_t)c->status, sp=(uint8_t)c->sp;
    uint16_t pc;
    unsigned signed_word=c->pc==0xd6afu,negative_a=0,negative_b=0,original_b=b;
    unsigned signed_byte=c->pc==0xd65bu;
    if(c->pc==0xd201u || c->pc==0xdcf7u || c->pc==0xd6f5u){
        unsigned base=r[0x2a]|((unsigned)r[0x2b]<<8),x=(uint8_t)(sp-6u),i,left=0,right=0,count=0,v;
        unsigned signed_long=c->pc==0xd6f5u,na=0,nb=0;
        unsigned wrapper=c->pc==0xdcf7u || signed_long,extra=0,outer_sp=sp;
        if(wrapper){
            uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FM_POINTER)c->read8;
            unsigned lp=r[0x20]|((unsigned)r[0x21]<<8),rp=r[0x23]|((unsigned)r[0x24]<<8);
            if(sp<18u || lp<0x400u || rp<0x400u || lp>0xfffcu || rp>0xfffcu)return 0;
            for(i=0;i<4u;++i){left|=(unsigned)read((uint16_t)(lp+i))<<(8u*i);right|=(unsigned)read((uint16_t)(rp+i))<<(8u*i);extra+=((lp&255u)+i>255u)+((rp&255u)+i>255u);}
            if(signed_long){
                na=left>>31;nb=right>>31;if(na)left=0u-left;if(nb)right=0u-right;
                extra+=na?(nb?329u:370u):(nb?370u:170u);
                if(na!=nb)for(i=1;i<4u;++i)extra+=((base+8u)&255u)+i>255u;
            }else extra+=155u;
            sp=(uint8_t)(sp-10u);x=(uint8_t)(sp-6u);
        }
        if((p&8u) || sp<8u || sp>245u || base<0x400u || base>0xff4u)return 0;
        if(!wrapper)for(i=0;i<4u;++i){left|=(unsigned)r[0x100u+sp+3u+i]<<(i*8u);right|=(unsigned)r[0x100u+sp+7u+i]<<(i*8u);}
        for(v=left;v;v>>=1)count+=v&1u;
        cost=!left?144u:!right?162u:1528u+53u*count;
        cost+=extra;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(wrapper){
            for(i=0;i<4u;++i){r[0x103u+sp+i]=(uint8_t)(left>>(i*8u));r[0x107u+sp+i]=(uint8_t)(right>>(i*8u));}
            unsigned ret=signed_long?(na?(nb?0xd749u:0xd728u):(nb?0xd736u:0xd718u)):0xdd10u;
            r[0x101u+sp]=(uint8_t)ret;r[0x102u+sp]=(uint8_t)(ret>>8);
        }
        product=left*right;
        r[0x101u+x]=left&&right?8u:12u;r[0x102u+x]=0;
        for(i=0;i<4u;++i){r[0x103u+x+i]=(uint8_t)(product>>(8u*i));r[base+8u+i]=(uint8_t)(product>>(8u*i));}
        r[0x100u+x]=0xd2;r[0x0ffu+x]=0x7c;r[0x0feu+x]=left&&right?8u:0;
        r[0x20]=(uint8_t)(base+8u);r[0x21]=(uint8_t)((base+8u)>>8);
        c->ac=c->ix=sp;c->iy=3;
        c->status=(p&~0xc3u)|(sp&128u)|((x<128u && sp>=128u)?64u:0u)|(sp?0u:2u);
        if(wrapper){
            sp=(uint8_t)outer_sp;c->ac=c->ix=sp;
            c->status=(p&~0xc3u)|(sp&128u)|((sp>=128u && sp<136u)?64u:0u);
        }
        if(signed_long && na!=nb){
            unsigned ret=na?0xd72bu:0xd739u;
            product=0u-product;for(i=0;i<4u;++i)r[base+8u+i]=(uint8_t)(product>>(8u*i));
            r[0x100u+outer_sp-8u]=(uint8_t)(ret>>8);r[0x100u+outer_sp-9u]=(uint8_t)ret;
            r[0x100u+outer_sp-10u]=0xd7;r[0x100u+outer_sp-11u]=0xdf;
            r[0x100u+outer_sp-12u]=(uint8_t)(product>>24);c->iy=11u;
        }
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(signed_word){
        negative_a=a&0x8000u;negative_b=b&0x8000u;
        if(negative_b){p=(p&~0x41u)|(b==0x8000u?64u:0u);b=(uint16_t)(0u-b);}
        if(negative_a){p=(p&~0x41u)|(a==0x8000u?64u:0u);a=(uint16_t)(0u-a);}
        bits=last=b;product=a*b;
    }
    if(c->pc==0xdcefu)a=(a&0xff00u)|(uint8_t)c->ac;
    if(c->pc==0xd184u || c->pc==0xdcefu || signed_byte){
        unsigned aa=(uint8_t)a,bb=(uint8_t)b,partial=bb;
        if(p&8u)return 0u;
        if(signed_byte){
            aa=(uint8_t)c->ac;negative_a=aa&128u;negative_b=bb&128u;
            if(negative_a){p=(p&~0x41u)|(aa==128u?64u:0u);aa=(uint8_t)(0u-aa);}
            if(negative_b){p=(p&~0x41u)|(bb==128u?64u:0u);bb=(uint8_t)(0u-bb);}
            partial=bb;
        }
        bits=bb-((bb>>1)&0x55u);bits=(bits&0x33u)+((bits>>2)&0x33u);
        bits=(bits+(bits>>4))&15u;
        cost=!aa?12u:!bb?17u:153u+4u*bits;
        if(c->pc==0xdcefu)cost+=6u;
        if(signed_byte)cost+=negative_b?(negative_a?63u:58u):(negative_a?41u:24u);
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        if(c->pc==0xdcefu)r[0x20]=(uint8_t)aa;
        if(signed_byte){
            unsigned ret=negative_b?(negative_a?0xd6a8u:0xd689u):(negative_a?0xd676u:0xd667u);
            r[0x20]=(uint8_t)aa;
            if(negative_b){r[0x100u|sp]=(uint8_t)original_b;--sp;r[0x23]=(uint8_t)bb;}
            r[0x100u|sp]=(uint8_t)(ret>>8);--sp;r[0x100u|sp]=(uint8_t)ret;--sp;
        }
        product=(uint8_t)(aa*bb);
        if(aa && bb){
            r[0x100u|sp]=(uint8_t)bb;
            while(!(partial&1u))partial>>=1;
            before=(uint8_t)((uint8_t)(aa*(partial>>1))<<1);sum=aa+before;
            p=(uint8_t)((p&~0x41u)|(((aa^sum)&(before^sum)&128u)?64u:0u)|
                ((bb&1u) && sum>255u?1u:0u));
            c->ix=product;
        }
        if(signed_byte){
            sp=(uint8_t)(sp+2u);
            if((negative_a!=0u)!=(negative_b!=0u)){
                p=(p&~0x41u)|(product==0u)|(product==128u?64u:0u);product=(uint8_t)(0u-product);
            }
            if(negative_b){c->ix=product;++sp;r[0x23]=r[0x100u|sp];}
        }
        c->ac=product;c->status=(p&~0x82u)|(product&128u)|(product?0u:2u);
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;
        FW_RECORD(c,cost);return cost;
    }
    if((c->pc!=0xd1a2u && c->pc!=0xdcf4u && !signed_word) || (p&8u))return 0u;
    bits=bits-((bits>>1)&0x5555u);
    bits=(bits&0x3333u)+((bits>>2)&0x3333u);
    bits=(bits+(bits>>4))&0x0f0fu;
    bits=(bits+(bits>>8))&31u;
    cost=!a?61u:!b?69u:((b&0xff00u)?443u:259u)+19u*bits;
    if(c->pc==0xdcf4u)cost+=3u;
    if(signed_word)cost+=negative_b?(negative_a?117u:116u):(negative_a?83u:22u);
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
    if(signed_word){
        unsigned call_return=negative_b?(negative_a?0xd6edu:0xd6d4u):(negative_a?0xd6c4u:0xd6b9u);
        if(negative_b){r[0x100u|sp]=(uint8_t)(original_b>>8);--sp;r[0x100u|sp]=(uint8_t)original_b;--sp;}
        r[0x100u|sp]=(uint8_t)(call_return>>8);--sp;r[0x100u|sp]=(uint8_t)call_return;--sp;
    }
    r[0x100u|sp]=(uint8_t)(b>>8);--sp;
    r[0x100u|sp]=(uint8_t)b;--sp;
    if(a && b){
        p=fw_mul16_status(a,b,p);
        c->ix=0u;
    }
    r[0x26]=r[0x20]=(uint8_t)product;
    r[0x27]=r[0x21]=(uint8_t)(product>>8);
    ++sp;r[0x23]=r[0x100u|sp];++sp;r[0x24]=r[0x100u|sp];
    c->ac=r[0x24];
    p=(uint8_t)((p&~0x82u)|(c->ac&0x80u)|(c->ac?0u:2u));
    ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
    if(signed_word){
        if((negative_a!=0u)!=(negative_b!=0u)){
            unsigned value=(uint16_t)product,negated=(uint16_t)(0u-value);
            unsigned call_return=negative_b?0xd6d7u:0xd6c7u;
            r[0x100u|sp]=(uint8_t)(call_return>>8);r[0x100u|(uint8_t)(sp-1u)]=(uint8_t)call_return;
            r[0x20]=(uint8_t)negated;r[0x21]=(uint8_t)(negated>>8);c->ac=negated>>8;
            p=(p&~0x41u)|(value==0u)|(value==0x8000u?64u:0u);
        }
        if(negative_b){++sp;r[0x23]=r[0x100u|sp];++sp;r[0x24]=r[0x100u|sp];c->ac=r[0x24];}
        p=(uint8_t)((p&~0x82u)|(c->ac&128u)|(c->ac?0u:2u));
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
    }
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->status=p;c->cycles+=cost;
    FW_RECORD(c,cost); return cost;
}
