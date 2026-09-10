/* Handwritten firmware contracts; no generated 6502 instruction bodies.
 * One relocatable function per module. Never calls the 9288 OS directly. */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#include "firmware_native_graphics_abi.h"
#define FW_POINTER unsigned long
#else
#define FW_POINTER uintptr_t
#endif

typedef uint8_t (*fw_read_fn)(uint16_t);
#include "firmware_native_metrics.h"
typedef void (*fw_write_fn)(uint16_t, uint8_t);

__attribute__((used, noinline, section(".text.firmware_native_bank")))
uint32_t firmware_native_bank(s6502_iram_asm_context_t *c)
{
    fw_read_fn read = (fw_read_fn)(FW_POINTER)c->read8;
    fw_write_fn write = (fw_write_fn)(FW_POINTER)c->write8;
    uint8_t *ram = (uint8_t *)(FW_POINTER)c->ram;
    uint8_t a = (uint8_t)c->ac, x = (uint8_t)c->ix;
    uint8_t y = (uint8_t)c->iy, sp = (uint8_t)c->sp;
    uint16_t pointer, destination, pc = (uint16_t)c->pc;
    uint32_t cost, count, selected;
    uint32_t stage=0,total=0;
    uint8_t p = (uint8_t)c->status;
    uint32_t *bm=0;
#if !defined(FIRMWARE_NATIVE_HOST_TEST) || defined(FW_NATIVE_BANK_TEST_SERVICES)
    firmware_native_graphics_services_t *services=(firmware_native_graphics_services_t *)(FW_POINTER)c->graphics;
    if(services && services->version==5u)bm=(uint32_t *)(FW_POINTER)services->bank_metrics;
    else services=0;
#endif
#define R16(addr) ((uint16_t)(read((uint16_t)(addr)) | \
    ((uint16_t)read((uint16_t)((addr) + 1u)) << 8)))
#define PUSH(v) do { ram[0x100u | sp] = (uint8_t)(v); --sp; } while (0)
#define POP() ram[0x100u | (sp = (uint8_t)(sp + 1u))]
    if(pc==0xd2f6u){
        uint32_t safe;
        if(bm)++bm[3];
        pointer=R16(0x26u);
        if(p&8u){if(bm)++bm[4];return 0u;}
        safe=pointer>=0x400u && pointer<=0xffdu;
#if !defined(FIRMWARE_NATIVE_HOST_TEST) || defined(FW_NATIVE_BANK_TEST_SERVICES)
        if(!safe && services && services->bank_descriptor_safe)
            safe=((uint32_t (*)(uint32_t))(FW_POINTER)services->bank_descriptor_safe)(pointer);
#endif
        if(!safe){if(bm){if(!bm[5])bm[8]=pointer;++bm[5];bm[9]=pointer;}return 0u;}
        cost=104u+110u+(read((uint16_t)(pointer+2u))<0xe0u?207u:213u)+
            ((pointer&255u)>=254u)+((pointer&255u)==255u);
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles){if(bm)++bm[6];return 0u;}
        if(bm){++bm[7];if(pointer>0xffdu){if(!bm[10])bm[11]=pointer;++bm[10];}}
        x=a;p=(p&~0x82u)|(a&128u)|(a?0u:2u);PUSH(0xd2);PUSH(0xf9);
        pc=0xf4a5u;stage=1;total=11u;
    }else if(pc==0xd313u){
        cost=23u+(ram[0x100u|(uint8_t)(sp+1u)]<0xe0u?207u:213u);
        if((p&8u) || c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        x=a;a=POP();p=(p&~0x82u)|(a&128u)|(a?0u:2u);
        PUSH(0xd3);PUSH(0x17);pc=0xf52au;stage=3;total=15u;
    }
bank_entry:
    if(pc==0xf4a5u || pc==0xf4adu || pc==0xf4afu){
        uint16_t bank,base;
        uint8_t bias;
        uint32_t prefix=pc==0xf4a5u?0u:pc==0xf4adu?17u:20u;
        if(p&8u)return 0u;
        /* Reserve the longer branch before touching the bank selector. */
        cost=110u-prefix;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        if(pc==0xf4a5u){PUSH(p|0x30u);PUSH(x);PUSH(y);}
        if(pc!=0xf4afu)write(0x0cu,5u);
        bank=R16(0x0cu+1u);
        bias=(bank>>8)<read(0x03d5u)?0xe0u:0u;
        if(!bias)cost-=2u;
        write(0x2000u,bias);
        base=bias?R16(0x2029u):(uint16_t)(read(0x03d6u)|((uint16_t)read(0x03d5u)<<8));
        write(0x2000u,(uint8_t)(((uint16_t)(bank-base)>>2)+bias));
        y=POP();x=POP();a=read(0x2000u);
        goto finished;
    }
    if(pc==0xf52au || pc==0xf549u || pc==0xf55bu){
        uint16_t base,bank;
        unsigned i;
        if(p&8u)return 0u;
        cost=pc==0xf52au?(a<0xe0u?207u:213u):pc==0xf549u?185u:161u;
        if(c->cycles>c->cycle_budget || cost>c->cycle_budget-c->cycles)return 0u;
        if(pc==0xf52au){
            PUSH(p|0x30u);write(0x2000u,a);PUSH(x);PUSH(y);a=read(0x2000u);
        }
        if(pc==0xf549u || (pc==0xf52au && a>=0xe0u)){
            a=(uint8_t)(a-0xe0u);write(0x2000u,a);
            write(0x2001u,read(0x2029u));write(0x2002u,read(0x202au));
        }else if(pc==0xf52au){
            write(0x2001u,read(0x03d6u));write(0x2002u,read(0x03d5u));
        }
        write(0x0cu,5u);base=R16(0x2001u);
        bank=(uint16_t)(base+4u*read(0x2000u));
#if !defined(FIRMWARE_NATIVE_HOST_TEST) || defined(FW_NATIVE_BANK_TEST_SERVICES)
        if(services && services->bank_map4 &&
           ((uint32_t (*)(uint32_t))(FW_POINTER)services->bank_map4)(bank)){
            y=POP();a=POP();x=a;goto finished;
        }
#endif
        write(0x0du,(uint8_t)bank);a=(uint8_t)(bank>>8);
        write(0x0eu,a);write(0x2000u,a);
        for(i=0;i<3u;++i){
            uint16_t next=(uint16_t)(read(0x0du)+1u);
            write(0x0cu,(uint8_t)(read(0x0cu)+1u));write(0x0du,(uint8_t)next);
            a=(uint8_t)(read(0x2000u)+(next>>8));write(0x0eu,a);write(0x2000u,a);
        }
        y=POP();a=POP();x=a;goto finished;
    }
    if (pc != 0xf457u && pc != 0xf475u && pc != 0xf48bu) return 0u;
    pointer = R16(0x28u);
    if (pc != 0xf48bu && (pointer < 0x400u || pointer > 0xffdu))
        return 0u; /* Fixed RAM only: no stack aliases through bank windows. */
    if (pc == 0xf457u) {
        destination = R16(pointer);
        if (destination < 0x400u || destination > 0xffeu) return 0u;
        cost = 67u + ((pointer & 255u) == 255u);
        if (c->cycles > c->cycle_budget || cost > c->cycle_budget-c->cycles)
            return 0u;
        PUSH(p | 0x30u); PUSH(a);
        write(0x2fu, read(pointer));
        write(0x30u, read((uint16_t)(pointer + 1u)));
        a = POP(); write(0x0cu, a);
        a = read(0x0du); write(R16(0x2fu), a);
        a = read(0x0eu); y = 1u;
        write((uint16_t)(R16(0x2fu) + 1u), a);
    } else {
        if (p & 8u) return 0u;
        selected = pc == 0xf475u ? a & 15u : read(0x0cu);
        count = pc == 0xf475u ? read(pointer) : x;
        if (pc == 0xf475u) { if (!count) return 0u; --count; }
        if (selected < 5u || selected > 14u || count > 14u-selected)
            return 0u;
        cost = count * 36u + 19u;
        if (pc == 0xf475u)
            cost += 42u + ((pointer & 255u) == 255u) + ((pointer & 255u) >= 254u);
        if (c->cycles > c->cycle_budget || cost > c->cycle_budget-c->cycles)
            return 0u;
        if (pc == 0xf475u) {
            PUSH(p | 0x30u); write(0x0cu, a);
            a = read((uint16_t)(pointer+1u)); write(0x0du, a);
            a = read((uint16_t)(pointer+2u)); write(0x0eu, a);
            PUSH(a); y = 0u; x = (uint8_t)count;
        }
        while (x) {
            uint16_t next = (uint16_t)(read(0x0du)+1u);
            write(0x0cu, (uint8_t)(read(0x0cu)+1u));
            write(0x0du, (uint8_t)next);
            a = (uint8_t)(POP() + (next >> 8));
            write(0x0eu, a); PUSH(a); --x;
        }
        a = POP();
    }
finished:
    p = POP() | 0x30u;
    pc = POP(); pc |= (uint16_t)POP() << 8;
    if(stage==1u){
        total+=cost+19u;PUSH(a);y=2u;a=read((uint16_t)(pointer+2u));
        p=(p&~0x82u)|(a&128u)|(a?0u:2u);PUSH(0xd3);PUSH(1u);
        pc=0xf52au;stage=2u;goto bank_entry;
    }else if(stage==2u){
        unsigned target,minus,high,sum;
        total+=cost+74u+((pointer&255u)>=254u)+((pointer&255u)==255u);
        a=read((uint16_t)(pointer+1u));PUSH(a);a=read(pointer);ram[0x26]=a;
        a=POP();ram[0x27]=a;a=x;PUSH(0xd3);PUSH(0x12);
        target=R16(0x26u);minus=(uint16_t)(target-1u);high=target>>8;
        sum=high+255u+((target&255u)!=0u);ram[0x26]=(uint8_t)minus;ram[0x27]=(uint8_t)(minus>>8);
        PUSH(minus>>8);PUSH(minus);sp=(uint8_t)(sp+2u);y=a;
        p=(p&~0xc3u)|(sum>255u)|(((high^sum)&(255u^sum)&128u)?64u:0u)|(a&128u)|(a?0u:2u);
        pc=(uint16_t)(target-1u);cost=total;
    }else if(stage==3u){
        total+=cost+8u;a=x;p=(p&~0x82u)|(a&128u)|(a?0u:2u);
        pc=POP();pc|=(uint16_t)POP()<<8;cost=total;
    }
    c->pc = (uint16_t)(pc+1u); c->ac = a; c->ix = x; c->iy = y;
    c->sp = sp; c->status = p; c->cycles += cost;
    FW_RECORD(c,cost); return cost;
#undef R16
#undef PUSH
#undef POP
}
