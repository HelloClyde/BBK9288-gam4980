/* C6502 integer/long operations, conversions and temporary-result ABI. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#define FR_POINTER unsigned long
#else
#define FR_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
#include "firmware_native_runtime_direct.h"
__attribute__((used,noinline,section(".text.firmware_native_runtime")))
uint32_t firmware_native_runtime(s6502_iram_asm_context_t *c)
{
    uint8_t *r=(uint8_t *)(FR_POINTER)c->ram;
    uint8_t (*read)(uint16_t)=(uint8_t (*)(uint16_t))(FR_POINTER)c->read8;
    void (*write)(uint16_t,uint8_t)=(void (*)(uint16_t,uint8_t))(FR_POINTER)c->write8;
    uint32_t cost=31u,left=0,right=0,out=0,i,sum,high,add=8u,dest=0x20u;
    uint8_t a=(uint8_t)c->ac,p=(uint8_t)c->status,sp=(uint8_t)c->sp;
    uint16_t pc=(uint16_t)c->pc;
    unsigned longop=pc==0xd2cau?1u:pc==0xd8bdu?2u:pc==0xddb8u?3u:
        pc==0xd29du?4u:pc==0xdb2fu?5u:pc==0xd780u?6u:pc==0xd7b4u?7u:pc==0xd7f1u?8u:0u;
#define R16(z) ((uint16_t)(r[z]|((uint16_t)r[(z)+1u]<<8)))
#define PUSH(v) do { r[0x100u|sp]=(uint8_t)(v);--sp; } while(0)
#define POP() r[0x100u|(sp=(uint8_t)(sp+1u))]
    if(p&8u)return 0u;
    if(pc==0xd752u){
        unsigned index=sp+a,value=0;
        if(index>250u)return 0u;
        cost=72u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        for(i=0;i<4u;++i)value|=(unsigned)r[0x102u+index+i]<<(8u*i);
        out=0u-value;for(i=0;i<4u;++i)r[0x102u+index+i]=(uint8_t)(out>>(8u*i));
        c->ix=index;c->iy=r[0x20];c->ac=out>>24;
        c->status=(p&~0xc3u)|(value==0u)|(value==0x80000000u?64u:0u)|(c->ac&128u)|(c->ac?0u:2u);
        pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);c->sp=sp;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xda3du || pc==0xdc0eu || pc==0xd93fu){
        unsigned source=R16(0x20),counts=R16(0x23),base=R16(0x2a),upper=0,count,value=0,ret;
        unsigned signed_shift=pc==0xd93fu,right_shift=pc!=0xda3du,negative=0;
        if(source<0x400u || source>0xfffcu || counts<0x400u || counts>0xfffcu ||
           base<0x400u || base>0xff4u ||
           (source<base+12u && source+4u>base+8u) ||
           (counts<base+12u && counts+4u>base+8u))return 0u;
        cost=0;
        if(signed_shift){
            negative=read((uint16_t)(source+3u))&128u;
            cost=(negative?10u:12u)+((source&255u)+3u>255u);
        }
        for(i=1;i<4u;++i){upper|=read((uint16_t)(counts+i));cost+=((counts&255u)+i)>255u;}
        count=upper?32u:read((uint16_t)counts);
        if(upper){cost+=123u;a=(uint8_t)upper;ret=signed_shift?0xd964u:right_shift?0xdc28u:0xda57u;value=negative?0xffffffffu:0u;}
        else if(count>=32u){cost+=135u;a=(uint8_t)count;ret=signed_shift?0xd96fu:right_shift?0xdc33u:0xda62u;value=negative?0xffffffffu:0u;}
        else{
            cost+=count?202u+65u*count:165u;
            /* This ROM's LDY #$0b immediately before BMI clears N.
             * Its normal-count path is logical even for a negative operand. */
            if(signed_shift)cost+=2u;
            ret=signed_shift?0xd9b7u:right_shift?0xdc79u:0xdaa8u;
            for(i=0;i<4u;++i){
                value|=(unsigned)read((uint16_t)(source+i))<<(8u*i);
                cost+=((source&255u)+i)>255u;
                cost+=count*(((base&255u)+8u+i)>255u);
            }
            if(count)value=right_shift?value>>count:value<<count;
            a=(uint8_t)(right_shift && count?value:value>>24);
        }
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        for(i=0;i<4u;++i)r[base+8u+i]=(uint8_t)(value>>(8u*i));
        c->ix=0;c->iy=upper?1u:count>=32u?0u:right_shift && count?8u:11u;
        if(count){
            PUSH(ret>>8);PUSH(ret);PUSH(a);
            sum=(base>>8)+(((base&255u)+8u)>>8);
            r[0x20]=(uint8_t)(base+8u);r[0x21]=(uint8_t)sum;
            p=(uint8_t)((p&~0x41u)|(sum>255u)|((((base>>8)^sum)&sum&128u)?64u:0u));
            a=POP();sp=(uint8_t)(sp+2u);p=(uint8_t)((p&~0x82u)|(a&128u)|(a?0u:2u));
        }else p=(uint8_t)((p&~0x83u)|3u);
        c->ac=a;c->status=p;pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);c->sp=sp;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xdae6u || pc==0xdb19u || pc==0xdac1u || pc==0xdac7u){
        unsigned reverse=pc==0xdb19u || pc==0xdac7u;
        unsigned source=R16(reverse?0x20u:0x23u),target=R16(reverse?0x23u:0x20u);
        if(source<0x400u || target<0x400u || source>0xfffcu || target>0xfffcu)return 0u;
        cost=(pc==0xdac1u || pc==0xdac7u)?61u:58u;
        for(i=1;i<4u;++i)cost+=((source&255u)+i)>255u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        for(i=0;i<4u;++i){a=read((uint16_t)(source+i));write((uint16_t)(target+i),a);}
        c->ac=a;c->iy=3u;c->status=(p&~0x82u)|(a&128u)|(a?0u:2u);
        pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);c->sp=sp;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xdaaau || pc==0xdacau || pc==0xdafcu || pc==0xdac4u){
        unsigned size=pc==0xdaaau?1u:pc==0xdacau?2u:4u;
        unsigned target=R16(0x28),source=R16(0x20);
        if(target<0x400u+size || (size==4u && (source<0x400u || source>0xfffcu)))return 0u;
        cost=size==1u?45u:size==2u?55u:pc==0xdac4u?111u:108u;
        if(size==4u)for(i=1;i<4u;++i)cost+=((source&255u)+i)>255u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        PUSH(p|0x30u);target-=size;r[0x28]=(uint8_t)target;r[0x29]=(uint8_t)(target>>8);
        if(size==1u){c->ix=a;write((uint16_t)target,a);c->iy=0;}
        else{
            for(i=0;i<size;++i){a=size==2u?r[0x20u+i]:read((uint16_t)(source+i));write((uint16_t)(target+i),a);}
            c->iy=size==2u?1u:4u;if(size==4u)c->ix=0;
        }
        c->ac=a;c->status=POP();pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);c->sp=sp;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd9dfu || pc==0xd9f3u){
        unsigned value,doubled,total;
        dest=pc==0xd9dfu?0x20u:0x23u;cost=pc==0xd9dfu?40u:47u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        value=R16(dest);doubled=(uint16_t)(value<<1);total=value+doubled;
        if(pc==0xd9f3u){PUSH(a);a=POP();}else a=(uint8_t)(total>>8);
        r[dest]=(uint8_t)total;r[dest+1u]=(uint8_t)(total>>8);
        c->ix=(uint8_t)value;c->iy=value>>8;
        p=(uint8_t)((p&~0xc3u)|(total>65535u)|
            (((value^total)&(doubled^total)&0x8000u)?64u:0u)|(a&128u)|(a?0u:2u));
        pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=p;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xde02u){
        unsigned pointer=R16(0x20),target=R16(0x2a),value=0,y;
        if(pointer<0x400u || pointer>0xfffcu || target<0x400u || target>0xff00u)return 0u;
        cost=0;
        for(i=0;i<4u;++i){value|=read((uint16_t)(pointer+i));cost+=((pointer&255u)+i)>255u;}
        cost+=value?75u:77u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        /* This ROM really uses LDY absolute $0008, not LDY immediate #8. */
        y=read(8u);write((uint16_t)(target+y),(uint8_t)!value);
        for(i=0;i<3u;++i){y=(uint8_t)(y+1u);write((uint16_t)(target+y),0u);}
        c->ac=0;c->iy=y;c->status=(p&~0x82u)|(y&128u)|(y?0u:2u);
        pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);c->sp=sp;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd4a9u || pc==0xd4c6u || pc==0xd519u || pc==0xd53au ||
       pc==0xdd1fu || pc==0xdd38u || pc==0xdd58u || pc==0xdd75u){
        unsigned unsigned_mode=pc>=0xdd00u;
        unsigned second=pc==0xd519u || pc==0xd53au || pc==0xdd58u || pc==0xdd75u;
        unsigned word=pc==0xd4c6u || pc==0xd53au || pc==0xdd38u || pc==0xdd75u;
        unsigned preserve_a=second && (!unsigned_mode || !word);
        unsigned pointer=R16(0x2a),value=second?r[0x23]:word?r[0x20]:a;
        unsigned sign=word?r[second?0x24u:0x21u]:value;
        unsigned fill=!unsigned_mode && (sign&128u)?255u:0u,offset=second?32u:16u,return_address;
        if(pointer<0x400u || pointer>0xffdcu)return 0u;
        cost=(second?(word?93u:90u):(word?86u:80u))+(fill!=0u);
        if(unsigned_mode)cost=word?83u:second?87u:77u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        if(preserve_a)PUSH(a);
        write((uint16_t)(pointer+offset),(uint8_t)value);
        write((uint16_t)(pointer+offset+1u),(uint8_t)(word?sign:fill));
        write((uint16_t)(pointer+offset+2u),(uint8_t)fill);
        write((uint16_t)(pointer+offset+3u),(uint8_t)fill);
        return_address=pc==0xd4a9u?0xd4beu:pc==0xd4c6u?0xd4dfu:pc==0xd519u?0xd531u:0xd554u;
        if(unsigned_mode)return_address=pc==0xdd1fu?0xdd30u:pc==0xdd38u?0xdd4du:pc==0xdd58u?0xdd6cu:0xdd8au;
        PUSH(return_address>>8);PUSH(return_address);PUSH(fill);
        sum=(pointer>>8)+(((pointer&255u)+offset)>>8);dest=second?0x23u:0x20u;
        r[dest]=(uint8_t)(pointer+offset);r[dest+1u]=(uint8_t)sum;
        p=(uint8_t)((p&~0x41u)|(sum>255u)|((((pointer>>8)^sum)&sum&128u)?64u:0u));
        a=POP();sp=(uint8_t)(sp+2u);if(preserve_a)a=POP();
        c->iy=offset+3u;pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=(p&~0x82u)|(a&128u)|(a?0u:2u);
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd49eu || pc==0xd50cu || pc==0xd85fu || pc==0xd8aeu || pc==0xd85au || pc==0xd8a5u){
        unsigned pointer=0;
        if(pc==0xd49eu)cost=(a&128u)?18u:17u;
        else if(pc==0xd50cu)cost=(r[0x23]&128u)?25u:24u;
        else if(pc==0xd85au || pc==0xd8a5u){
            dest=pc==0xd85au?0x20u:0x23u;pointer=R16(dest);
            if(pointer<0x300u)return 0u;
            cost=pc==0xd85au?13u:23u;
        }
        else {dest=pc==0xd85fu?0x20u:0x23u;pointer=R16(dest);
            if(pointer<0x300u || pointer>0xfffeu)return 0u;
            cost=(pc==0xd85fu?28u:35u)+((pointer&255u)==255u);}
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        if(pc==0xd49eu){r[0x20]=a;a=(a&128u)?255u:0u;r[0x21]=a;}
        else if(pc==0xd50cu){PUSH(a);r[0x24]=(r[0x23]&128u)?255u:0u;a=POP();}
        else if(pc==0xd85au || pc==0xd8a5u){
            if(pc==0xd8a5u)PUSH(a);
            c->iy=0u;a=read((uint16_t)pointer);
            if(pc==0xd8a5u){r[0x23]=a;a=POP();}
        }
        else{
            if(pc==0xd8aeu)PUSH(a);
            c->ix=read((uint16_t)pointer);c->iy=1u;
            a=read((uint16_t)(pointer+1u));r[dest+1u]=a;r[dest]=(uint8_t)c->ix;
            if(pc==0xd8aeu)a=POP();
        }
        pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=(p&~0x82u)|(a&128u)|(a?0u:2u);
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd90bu){
        unsigned count=r[0x23],upper=r[0x24],value=R16(0x20),negative=value&0x8000u;
        cost=upper?(negative?26u:25u):!count?16u:count>=16u?
            (negative?35u:34u):negative?27u+17u*count:26u+15u*count;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        c->ix=upper?upper:count;p=(uint8_t)((p&~0x82u)|2u);
        if(upper || count>=16u){
            a=negative?255u:0u;r[0x20]=r[0x21]=a;
            p=(uint8_t)((p&~0x82u)|(negative?128u:2u));
            if(!upper)p|=1u;
        }else if(count){
            unsigned result=(value>>count)|(negative?(0xffffu<<(16u-count)):0u);
            a=(uint8_t)(value>>8);r[0x20]=(uint8_t)result;r[0x21]=(uint8_t)(result>>8);
            c->ix=0u;p=(uint8_t)((p&~1u)|((value>>(count-1u))&1u));
        }
        pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=p;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd8e9u){
        unsigned count=r[0x23],negative=a&0x80u;
        cost=!count?11u:count>=8u?(negative?23u:22u):
            negative?21u+9u*count:20u+7u*count;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        c->ix=count;p=(uint8_t)((p&~0x82u)|2u);
        if(count>=8u){a=negative?255u:0u;p=(uint8_t)((p&~0x82u)|1u|(negative?0x80u:2u));}
        else if(count){
            p=(uint8_t)((p&~1u)|((a>>(count-1u))&1u));
            a=(uint8_t)((a>>count)|(negative?(255u<<(8u-count)):0u));c->ix=0u;
        }
        pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=p;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xda1au || pc==0xdbf2u){
        unsigned count=r[0x23],upper=r[0x24],value=R16(0x20),result;
        int left_shift=pc==0xda1au;
        cost=upper?(left_shift?19u:20u):!count?(left_shift?17u:16u):
            count>=16u?(left_shift?30u:29u):22u+15u*count;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        p=(uint8_t)((p&~0x82u)|2u);c->ix=upper?upper:count;
        if(upper || count>=16u){
            r[0x20]=r[0x21]=0u;a=0u;
            if(!upper)p|=1u;
        }else if(count){
            unsigned carry=left_shift?(value>>(16u-count))&1u:(value>>(count-1u))&1u;
            result=left_shift?value<<count:value>>count;
            r[0x20]=(uint8_t)result;r[0x21]=(uint8_t)(result>>8);
            c->ix=0u;p=(uint8_t)((p&~1u)|carry);
        }
        pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=p;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xdda7u || pc==0xdde4u || pc==0xddeeu || pc==0xda09u || pc==0xdbe1u){
        unsigned count=r[0x23],value=R16(0x20);
        cost=pc==0xdda7u?30u:pc==0xdde4u?(a?13u:12u):
            pc==0xddeeu?(value?25u:27u):(!count?11u:count>=8u?18u:16u+7u*count);
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        if(pc==0xdda7u){
            unsigned other=R16(0x23);
            r[0x20]=(uint8_t)other;r[0x21]=(uint8_t)(other>>8);
            r[0x23]=(uint8_t)value;r[0x24]=(uint8_t)(value>>8);
            a=(uint8_t)(value>>8);c->ix=other>>8;
            p=(uint8_t)((p&~0x82u)|(c->ix&0x80u)|(c->ix?0u:2u));
        }else if(pc==0xdde4u){
            a=!a;p=(uint8_t)((p&~0x82u)|(a?0u:2u));
        }else if(pc==0xddeeu){
            r[0x20]=!value;r[0x21]=0u;a=0u;p=(uint8_t)((p&~0x82u)|2u);
        }else{
            c->ix=count;
            p=(uint8_t)((p&~0x82u)|2u);
            if(count>=8u){a=0u;p|=1u;}
            else if(count){
                unsigned carry=pc==0xda09u?(a>>(8u-count))&1u:(a>>(count-1u))&1u;
                a=(uint8_t)(pc==0xda09u?a<<count:a>>count);
                c->ix=0u;p=(uint8_t)((p&~1u)|carry);
            }
        }
        pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=p;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd7a6u || pc==0xd7e1u){
        uint16_t value,result;
        dest=pc==0xd7a6u?0x20u:0x23u;cost=pc==0xd7a6u?24u:31u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        value=R16(dest);result=(uint16_t)(0u-value);
        if(pc==0xd7e1u){PUSH(a);a=POP();}else a=(uint8_t)(result>>8);
        r[dest]=(uint8_t)result;r[dest+1u]=(uint8_t)(result>>8);
        p=(uint8_t)((p&~0xc3u)|(value==0u)|(value==0x8000u?0x40u:0u)|
            (a&0x80u)|(a?0u:2u));
        pc=POP();pc|=(uint16_t)POP()<<8;
        c->pc=(uint16_t)(pc+1u);c->sp=sp;c->ac=a;c->status=p;
        c->cycles+=cost;FW_RECORD(c,cost);return cost;
    }
    if(pc==0xd572u)cost=37u;
    else if(longop){
        left=R16(0x20);right=R16(0x23);out=R16(0x2a);
        if(left<0x300u || (longop<6u && right<0x300u) || out<0x300u ||
           left>0xfffcu || (longop<6u && right>0xfffcu) || out>0xfff4u)return 0u;
        cost=longop==6u?105u:longop==7u?113u:longop==8u?111u:123u+(longop>=4u?2u:0u);
        for(i=1u;i<4u;++i)cost+=((left&255u)+i>255u)+(longop<6u && (right&255u)+i>255u);
    } else if(pc==0xd586u)add=16u;
    else if(pc==0xd5a6u){add=32u;dest=0x23u;}
    else if(pc==0xd5b6u){add=24u;dest=0x23u;}
    else if(pc!=0xd596u)return 0u;
    if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
    if(pc==0xd572u){
        uint16_t target=R16(0x26),minus=(uint16_t)(target-1u);
        high=target>>8;sum=high+255u+((target&255u)!=0u);
        r[0x26]=(uint8_t)minus;r[0x27]=(uint8_t)(minus>>8);
        PUSH(minus>>8);PUSH(minus);sp=(uint8_t)(sp+2u);
        p=(uint8_t)((p&~0x41u)|(sum>255u)|
            (((high^sum)&(255u^sum)&0x80u)?0x40u:0u));
        c->pc=target;c->iy=a;
    }else{
        if(longop){
            unsigned carry=longop==5u || longop==7u;
            if(!fw_runtime_long_direct(c,longop,left,right,out+8u,&a)) for(i=0;i<4u;++i){
                unsigned l=read((uint16_t)(left+i)),rr=longop<6u?read((uint16_t)(right+i)):0u;
                if(longop==8u)a=(uint8_t)~l;
                else if(longop==6u)a=(uint8_t)(l^(i==3u?128u:0u));
                else if(longop==7u){sum=(uint8_t)~l+carry;a=(uint8_t)sum;carry=sum>255u;}
                else if(longop==1u)a=(uint8_t)(l&rr);
                else if(longop==2u)a=(uint8_t)(l|rr);
                else if(longop==3u)a=(uint8_t)(l^rr);
                else {sum=l+(longop==5u?(uint8_t)~rr:rr)+carry;
                    a=(uint8_t)sum;carry=sum>255u;}
                write((uint16_t)(out+8u+i),a);
            }
            c->iy=11u;
            sum=(uint16_t)(pc+(longop==6u?36u:longop==8u?42u:longop>=4u?43u:42u));PUSH(sum>>8);PUSH(sum);
        }
        PUSH(a);out=R16(0x2a);sum=(out>>8)+(((out&255u)+add)>>8);
        r[dest]=(uint8_t)(out+add);r[dest+1u]=(uint8_t)sum;
        p=(uint8_t)((p&~0x41u)|(sum>255u)|
            ((((out>>8)^sum)&sum&0x80u)?0x40u:0u));
        a=POP();
        if(longop)sp=(uint8_t)(sp+2u);
        pc=POP();pc|=(uint16_t)POP()<<8;c->pc=(uint16_t)(pc+1u);
    }
    c->ac=a;c->sp=sp;c->status=(p&~0x82u)|(a&0x80u)|(a?0u:2u);
    c->cycles+=cost;FW_RECORD(c,cost);return cost;
#undef R16
#undef PUSH
#undef POP
}
