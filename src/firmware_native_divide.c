/* Unsigned byte/word division: native arithmetic, not a guest instruction loop. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FD_POINTER unsigned long
#else
#define FD_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
/* No compiler-rt dependency in a relocatable standalone module. */
static __attribute__((always_inline)) inline unsigned fd_quotient(unsigned a,unsigned b)
{
    unsigned q=0,shift=a>255u?16u:8u;
    if(a<b)return 0u;
    if(b==1u)return a;
    while(shift){--shift;if(a>=(b<<shift)){a-=b<<shift;q|=1u<<shift;}}
    return q;
}
__attribute__((used,noinline,section(".text.firmware_native_divide")))
uint32_t firmware_native_divide(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FD_POINTER)c->ram;
    unsigned a=r[0x20],b=r[0x23],q=0,rem=0,cost,bits,last,shift=0;
    uint8_t p=(uint8_t)c->status,sp=(uint8_t)c->sp;
    uint16_t pc;
    unsigned signed_byte=c->pc==0xd39bu || c->pc==0xd5c6u,na_byte=0,nb_byte=0,original_byte=b;
    unsigned signed_mod_byte=c->pc==0xd5c6u;
    if(c->pc==0xd0a8u || c->pc==0xdc83u || c->pc==0xdcc8u || c->pc==0xd435u || c->pc==0xd604u){
        unsigned base=r[0x2a]|((unsigned)r[0x2b]<<8),x=(uint8_t)(sp-1u),low=0,high=0,divisor=0,i,accepted=0,cross=0;
        unsigned long_mod=c->pc==0xd604u;
        unsigned modulus=c->pc==0xdcc8u || long_mod;
        unsigned signed_long=c->pc==0xd435u || long_mod,na=0,nb=0;
        unsigned wrapper=c->pc==0xdc83u || modulus || signed_long,extra=0,outer_sp=sp,original_low,active,zero_divisor;
        if(modulus){if(sp<19u)return 0;sp=(uint8_t)(sp-2u);}
        if(wrapper){
            uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FD_POINTER)c->read8;
            unsigned lp=r[0x20]|((unsigned)r[0x21]<<8),rp=r[0x23]|((unsigned)r[0x24]<<8);
            if(sp<17u || lp<0x400u || rp<0x400u || lp>0xfffcu || rp>0xfffcu)return 0;
            for(i=0;i<4u;++i){low|=(unsigned)read((uint16_t)(lp+i))<<(8u*i);divisor|=(unsigned)read((uint16_t)(rp+i))<<(8u*i);extra+=((lp&255u)+i>255u)+((rp&255u)+i>255u);}
            if(signed_long){
                na=low>>31;nb=divisor>>31;if(na)low=0u-low;if(nb)divisor=0u-divisor;
                extra+=na?(nb?340u:381u):(nb?381u:181u);
                if(na!=nb)for(i=1;i<4u;++i)extra+=((base+8u)&255u)+i>255u;
                if(long_mod)extra+=((lp&255u)+3u)>255u;
            }else extra+=166u;
            sp=(uint8_t)(sp-14u);x=(uint8_t)(sp-1u);
        }
        if((p&8u) || sp<3u || sp>241u || base<0x400u || base>0xfe4u)return 0;
        for(i=0;i<4u;++i){
            if(!wrapper){low|=(unsigned)r[0x104u+x+i]<<(8u*i);high|=(unsigned)r[0x108u+x+i]<<(8u*i);
            divisor|=(unsigned)r[0x10cu+x+i]<<(8u*i);}
            cross+=((base&255u)+8u+i)>255u;
        }
        original_low=low;active=low && divisor;zero_divisor=low && !divisor;
        q=0;
        if(low && divisor){
            for(i=0;i<33u;++i){
                unsigned take=high>=divisor;
                if(take){high-=divisor;++accepted;}
                q=(q<<1)|take;
                if(i<32u){high=(high<<1)|(low>>31);low<<=1;}
            }
            cost=5351u+53u*accepted+33u*cross;
        }else cost=low?311u:235u;
        cost+=extra;
        if(modulus){
            cost+=long_mod?(na?238u:118u):109u;
            for(i=0;i<4u;++i)cost+=((base&255u)+24u+i)>255u;
            if(long_mod && na)for(i=1;i<4u;++i)cost+=((base+8u)&255u)+i>255u;
        }
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(modulus){unsigned ret=long_mod?(na?0xd633u:0xd60cu):0xdccau;r[0x100u|outer_sp]=(uint8_t)(ret>>8);r[0x100u|(uint8_t)(outer_sp-1u)]=(uint8_t)ret;}
        if(wrapper){
            for(i=0;i<4u;++i){r[0x104u+x+i]=(uint8_t)(original_low>>(8u*i));r[0x108u+x+i]=0;r[0x10cu+x+i]=(uint8_t)(divisor>>(8u*i));}
            unsigned ret=signed_long?(na?(nb?0xd48fu:0xd46eu):(nb?0xd47cu:0xd45eu)):0xdca2u;
            r[0x101u+sp]=(uint8_t)ret;r[0x102u+sp]=(uint8_t)(ret>>8);
        }
        r[0x100u|sp]=active?0:33u;
        {
            if(active){
                r[0x100u|sp]=0;
                for(i=0;i<4u;++i){r[0x104u+x+i]=0;r[0x108u+x+i]=(uint8_t)(high>>(8u*i));}
            }
            if(zero_divisor){x=0;q=0xffffffffu;}
            for(i=0;i<4u;++i){r[base+8u+i]=(uint8_t)(q>>(8u*i));r[base+24u+i]=r[0x108u+x+i];}
            r[0x0ffu+sp]=0xd1;r[0x0feu+sp]=0x81;r[0x0fdu+sp]=r[0x10bu+x];
            r[0x20]=(uint8_t)(base+8u);r[0x21]=(uint8_t)((base+8u)>>8);
            c->ac=active?0:33u;c->ix=x;c->iy=27u;
            c->status=(p&~0xc3u)|(active?2u:0u);
        }
        if(wrapper){
            sp=(uint8_t)outer_sp;c->ac=c->ix=sp;
            c->status=(p&~0xc3u)|(sp&128u)|((sp>=128u && sp<140u)?64u:0u);
        }
        if(signed_long && na!=nb){
            unsigned ret=na?0xd471u:0xd47fu;
            unsigned inner_sp=outer_sp-(modulus?2u:0u);
            q=0u-q;for(i=0;i<4u;++i)r[base+8u+i]=(uint8_t)(q>>(8u*i));
            r[0x100u+inner_sp-12u]=(uint8_t)(ret>>8);r[0x100u+inner_sp-13u]=(uint8_t)ret;
            r[0x100u+inner_sp-14u]=0xd7;r[0x100u+inner_sp-15u]=0xdf;
            r[0x100u+inner_sp-16u]=(uint8_t)(q>>24);c->iy=11u;
        }
        if(modulus){
            unsigned value=0,ret=long_mod?0xd62fu:0xdcedu;
            for(i=0;i<4u;++i)value|=(unsigned)r[base+24u+i]<<(8u*i);
            if(long_mod && na)value=0u-value;
            for(i=0;i<4u;++i)r[base+8u+i]=(uint8_t)(value>>(8u*i));
            c->ac=value>>24;c->ix=(uint8_t)(outer_sp-2u);c->iy=11u;
            if(long_mod && na){
                ret=0xd659u;r[0x100u|(uint8_t)(outer_sp-2u)]=0xd7;r[0x100u|(uint8_t)(outer_sp-3u)]=0xdf;
                r[0x100u|(uint8_t)(outer_sp-4u)]=(uint8_t)c->ac;
            }else r[0x100u|(uint8_t)(outer_sp-2u)]=(uint8_t)c->ac;
            r[0x100u|outer_sp]=(uint8_t)(ret>>8);r[0x100u|(uint8_t)(outer_sp-1u)]=(uint8_t)ret;
            c->status=(p&~0xc3u)|(c->ac&128u)|(c->ac?0u:2u);
        }
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(c->pc==0xd032u || c->pc==0xdc80u || c->pc==0xd3efu || c->pc==0xdcb1u || c->pc==0xd5dcu || c->pc==0xdd8fu){
        unsigned pointer=r[0x2a]|((unsigned)r[0x2b]<<8),oldsp=sp;
        unsigned modulus=c->pc==0xdcb1u;
        unsigned signed_mod=c->pc==0xd5dcu;
        unsigned unscale=c->pc==0xdd8fu;
        unsigned signed_word=c->pc==0xd3efu || signed_mod,na=0,nb=0,original_b,original_a;
        a|=(unsigned)r[0x21]<<8;b|=(unsigned)r[0x24]<<8;
        if(p&8u)return 0;
        original_a=a;
        if((modulus || signed_mod) && !a){
            cost=modulus?19u:21u;
            if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
            c->ac=0;c->status=(p&~0x82u)|2u;
            ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
            c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
        }
        original_b=b;
        if(unscale)b=3u;
        if(signed_word){
            na=a&0x8000u;nb=b&0x8000u;
            if(nb){p=(p&~0x41u)|(b==0x8000u?64u:0u);b=(uint16_t)(0u-b);}
            if(na){p=(p&~0x41u)|(a==0x8000u?64u:0u);a=(uint16_t)(0u-a);}
        }
        if(a && b){
            if(sp<5u+(signed_word?(nb?4u:2u):modulus?2u:0u)+(signed_mod?2u:0u)+(unscale?4u:0u))return 0; /* Absolute-X does not wrap. */
            q=fd_quotient(a,b);rem=a-q*b;cost=1132u;bits=q;
            while(bits){cost+=25u*(bits&1u);bits>>=1;}
        }else cost=a?31u:30u;
        if(c->pc==0xdc80u)cost+=3u;
        if(unscale)cost+=51u;
        if(modulus)cost+=(a&0xff00u)?33u:38u;
        if(signed_word)cost+=nb?(na?118u:117u):(na?83u:22u);
        if(signed_mod)cost+=na?60u:(original_a&0xff00u)?32u:37u;
        if(!a && (pointer<0x400u || pointer>0xffe6u))return 0;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
        if(unscale){
            r[0x100u|sp]=(uint8_t)original_b;--sp;r[0x100u|sp]=(uint8_t)(original_b>>8);--sp;
            r[0x100u|sp]=0xdd;--sp;r[0x100u|sp]=0x9f;--sp;oldsp=sp;r[0x23]=3;r[0x24]=0;
        }
        if(modulus){r[0x100u|sp]=0xdc;--sp;r[0x100u|sp]=0xbe;--sp;oldsp=sp;}
        if(signed_mod){r[0x100u|sp]=0xd5;--sp;r[0x100u|sp]=na?0xf7:0xeb;--sp;}
        if(signed_word){
            unsigned call_return=nb?(na?0xd42du:0xd414u):(na?0xd404u:0xd3f9u);
            if(nb){
                r[0x100u|sp]=(uint8_t)(original_b>>8);--sp;r[0x100u|sp]=(uint8_t)original_b;--sp;
                r[0x100u|(uint8_t)(sp-2u)]=(uint8_t)original_b;
                r[0x23]=(uint8_t)b;r[0x24]=(uint8_t)(b>>8);
            }
            if(na){r[0x20]=(uint8_t)a;r[0x21]=(uint8_t)(a>>8);}
            r[0x100u|sp]=(uint8_t)(call_return>>8);--sp;r[0x100u|sp]=(uint8_t)call_return;--sp;
            oldsp=sp;
        }
        if(!a){
            void (*write)(uint16_t,uint8_t)=(void (*)(uint16_t,uint8_t))(FD_POINTER)c->write8;
            write((uint16_t)(pointer+24u),0);write((uint16_t)(pointer+25u),0);
            c->ac=0;c->iy=25u;p&=(uint8_t)~0x82u;
        }else if(!b){
            r[0x20]=r[0x21]=255u;c->ac=255u;p=(p&~0x82u)|128u;
        }else{
            r[0x20]=(uint8_t)q;r[0x21]=(uint8_t)(q>>8);
            r[0x26]=(uint8_t)rem;r[0x27]=(uint8_t)(rem>>8);
            r[0x100u|sp]=(uint8_t)(rem>>8);r[0x100u|(sp-1u)]=(uint8_t)rem;
            r[0x100u|(sp-2u)]=r[0x100u|(sp-3u)]=r[0x100u|(sp-4u)]=0;
            c->ac=c->ix=oldsp;
            p=(uint8_t)((p&~0xc3u)|(oldsp&128u)|((oldsp>=128u && oldsp<133u)?64u:0u));
        }
        ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        if(unscale){
            ++sp;r[0x24]=r[0x100u|sp];++sp;r[0x23]=r[0x100u|sp];c->ac=r[0x23];
            p=(p&~0x82u)|(c->ac&128u)|(c->ac?0u:2u);
            ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        }
        if(modulus){
            r[0x20]=r[0x26];r[0x21]=r[0x27];c->ac=r[0x27];
            p=(p&~0x82u)|(c->ac&128u)|(c->ac?0u:2u);
            ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        }
        if(signed_word){
            if((na!=0u)!=(nb!=0u)){
                unsigned value=r[0x20]|((unsigned)r[0x21]<<8),negated=(uint16_t)(0u-value);
                unsigned call_return=nb?0xd417u:0xd407u;
                r[0x100u|sp]=(uint8_t)(call_return>>8);r[0x100u|(uint8_t)(sp-1u)]=(uint8_t)call_return;
                r[0x20]=(uint8_t)negated;r[0x21]=(uint8_t)(negated>>8);c->ac=negated>>8;
                p=(p&~0xc3u)|(value==0u)|(value==0x8000u?64u:0u)|(c->ac&128u)|(c->ac?0u:2u);
            }
            if(nb){++sp;r[0x23]=r[0x100u|sp];++sp;r[0x24]=r[0x100u|sp];c->ac=r[0x24];
                p=(p&~0x82u)|(c->ac&128u)|(c->ac?0u:2u);}
            ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        }
        if(signed_mod){
            unsigned value=r[0x26]|((unsigned)r[0x27]<<8);
            if(na){
                r[0x100u|sp]=0xd6;r[0x100u|(uint8_t)(sp-1u)]=2;
                p=(p&~0x41u)|(value==0u)|(value==0x8000u?64u:0u);value=(uint16_t)(0u-value);
            }
            r[0x20]=(uint8_t)value;r[0x21]=(uint8_t)(value>>8);c->ac=value>>8;
            p=(p&~0x82u)|(c->ac&128u)|(c->ac?0u:2u);
            ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
        }
        c->status=p;
        c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if((c->pc!=0xd000u && c->pc!=0xdc7bu && c->pc!=0xdcabu && !signed_byte) || (p&8u))return 0;
    if(c->pc==0xdc7bu || c->pc==0xdcabu)a=(uint8_t)c->ac;
    if(signed_byte){
        a=(uint8_t)c->ac;na_byte=a&128u;nb_byte=b&128u;
        if(na_byte){p=(p&~0x41u)|(a==128u?64u:0u);a=(uint8_t)(0u-a);}
        if(nb_byte){p=(p&~0x41u)|(b==128u?64u:0u);b=(uint8_t)(0u-b);}
    }
    if(a && b){
        q=fd_quotient(a,b);rem=a-q*b;bits=q;cost=284u;
        while(bits){cost+=7u*(bits&1u);bits>>=1;}
    }else cost=a?22u:14u;
    if(c->pc==0xdc7bu)cost+=6u;
    if(c->pc==0xdcabu)cost+=21u;
    if(signed_byte)cost+=nb_byte?(na_byte?63u:58u):(na_byte?41u:24u);
    if(signed_mod_byte)cost+=na_byte?28u:23u;
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0;
    if(c->pc==0xdc7bu || c->pc==0xdcabu)r[0x20]=(uint8_t)a;
    if(c->pc==0xdcabu){r[0x100u|sp]=0xdc;--sp;r[0x100u|sp]=0xad;--sp;}
    if(signed_byte){
        unsigned ret=nb_byte?(na_byte?0xd3e8u:0xd3c9u):(na_byte?0xd3b6u:0xd3a7u);
        if(signed_mod_byte){
            c->ix=(uint8_t)c->ac;
            r[0x100u|sp]=0xd5;--sp;r[0x100u|sp]=na_byte?0xd5:0xce;--sp;
        }
        r[0x20]=(uint8_t)a;
        if(nb_byte){r[0x100u|sp]=(uint8_t)original_byte;--sp;r[0x23]=(uint8_t)b;}
        r[0x100u|sp]=(uint8_t)(ret>>8);--sp;r[0x100u|sp]=(uint8_t)ret;--sp;
    }
    if(a && b){
        if(q){
            last=q;while(!(last&1u)){last>>=1;++shift;}
            last=(a>>shift)-last*b;bits=last+b;
            p=(uint8_t)((p&~0x40u)|(((bits^b)&(bits^last)&128u)?64u:0u));
        }
        p&=(uint8_t)~1u;r[0x20]=0;r[0x21]=(uint8_t)rem;r[0x26]=(uint8_t)q;
        c->ix=0;c->ac=q;
    }else {c->ac=a?255u:0u;r[0x21]=(uint8_t)c->ac;}
    if(c->pc==0xdcabu){sp=(uint8_t)(sp+2u);c->ac=r[0x21];}
    if(signed_byte){
        sp=(uint8_t)(sp+2u);
        if((na_byte!=0u)!=(nb_byte!=0u)){
            unsigned value=c->ac;
            p=(p&~0x41u)|(value==0u)|(value==128u?64u:0u);c->ac=(uint8_t)(0u-value);
        }
        if(nb_byte){c->ix=c->ac;++sp;r[0x23]=r[0x100u|sp];}
        if(signed_mod_byte){
            sp=(uint8_t)(sp+2u);c->ac=r[0x21];
            if(na_byte){
                unsigned value=c->ac;
                p=(p&~0x41u)|(value==0u)|(value==128u?64u:0u);c->ac=(uint8_t)(0u-value);
            }
        }
    }
    c->status=(p&~0x82u)|(c->ac&128u)|(c->ac?0u:2u);
    ++sp;pc=r[0x100u|sp];++sp;pc|=(uint16_t)r[0x100u|sp]<<8;
    c->sp=sp;c->pc=(uint16_t)(pc+1u);c->cycles+=cost;
    FW_RECORD(c,cost);return cost;
}
