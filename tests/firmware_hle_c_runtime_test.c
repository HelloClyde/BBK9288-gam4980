#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x140u
#include "../src/gam4980_core.c"

typedef struct {
    uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles;
    uintptr_t ram,read8,write8;
} host_compare_context;
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_compare_context
#include "../src/firmware_native_compare.c"
#include "../src/firmware_native_runtime.c"
#undef s6502_iram_asm_context_t
#undef FIRMWARE_NATIVE_HOST_TEST

#define TEST_CASES 10000u

static uint32_t random_state = 0x4980d2cau;

static uint32_t next_random(void)
{
    uint32_t value = random_state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static int load_exact(const char *path, uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "rb");
    int ok;

    if (!file)
        return 0;
    ok = fread(data, 1u, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static int cpu_equal(const s6502_t *left, const s6502_t *right)
{
    return left->pc == right->pc && left->ac == right->ac &&
        left->ix == right->ix && left->iy == right->iy &&
        left->sp == right->sp && left->status == right->status;
}

static int compare_hle(
    uint16_t entry_pc, uint16_t cycles, uint16_t return_pc,
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu = sys.cpu;
    s6502_t reference_cpu;
    uint32_t reference_executed;
    uint32_t hle_executed;
    uint32_t hits_before;

    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);
    gam4980_set_firmware_hle_enabled(0);
    reference_executed = s6502_exec(&sys.cpu, cycles);
    reference_cpu = sys.cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    if (reference_executed != cycles || reference_cpu.pc != return_pc) {
        fprintf(stderr,
            "reference mismatch case=%lu entry=%04x cycles=%u "
            "executed=%lu pc=%04x expected=%04x\n",
            (unsigned long)case_id, entry_pc, cycles,
            (unsigned long)reference_executed, reference_cpu.pc, return_pc);
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    sys.cpu = initial_cpu;
    {
        host_compare_context c={0};
        c.pc=initial_cpu.pc;c.ac=initial_cpu.ac;c.ix=initial_cpu.ix;c.iy=initial_cpu.iy;
        c.sp=initial_cpu.sp;c.status=initial_cpu.status;c.cycle_budget=cycles;
        c.ram=(uintptr_t)sys.ram;c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;
        uint32_t got=(entry_pc==0xd340u || entry_pc==0xd362u)?
            firmware_native_compare(&c):firmware_native_runtime(&c);
        if(got!=cycles || c.pc!=reference_cpu.pc ||
           c.ac!=reference_cpu.ac || c.ix!=reference_cpu.ix || c.iy!=reference_cpu.iy ||
           c.sp!=reference_cpu.sp || c.status!=reference_cpu.status ||
           memcmp(sys.ram,reference_ram,GAM4980_RAM_SIZE)) {
            fprintf(stderr,"authored compare mismatch pc=%04x\n",entry_pc);return 0;
        }
        memcpy(sys.ram,initial_ram,GAM4980_RAM_SIZE);
    }
    if(entry_pc==0xd586u || entry_pc==0xd5a6u || entry_pc==0xd5b6u ||
       entry_pc==0xd7a6u || entry_pc==0xd7e1u || entry_pc==0xd29du ||
       entry_pc==0xd8bdu || entry_pc==0xdb2fu || entry_pc==0xddb8u ||
       entry_pc==0xda09u || entry_pc==0xdbe1u || entry_pc==0xdda7u ||
       entry_pc==0xdde4u || entry_pc==0xddeeu || entry_pc==0xd780u || entry_pc==0xd7b4u || entry_pc==0xd7f1u ||
       entry_pc==0xda1au || entry_pc==0xdbf2u || entry_pc==0xd8e9u || entry_pc==0xd90bu ||
       entry_pc==0xd49eu || entry_pc==0xd50cu || entry_pc==0xd85fu || entry_pc==0xd8aeu ||
       entry_pc==0xd85au || entry_pc==0xd8a5u || entry_pc==0xd4a9u || entry_pc==0xd4c6u ||
       entry_pc==0xd519u || entry_pc==0xd53au || entry_pc==0xdd1fu || entry_pc==0xdd38u ||
       entry_pc==0xdd58u || entry_pc==0xdd75u || entry_pc==0xde02u || entry_pc==0xd9dfu || entry_pc==0xd9f3u ||
       entry_pc==0xdae6u || entry_pc==0xdb19u || entry_pc==0xdac1u || entry_pc==0xdac7u ||
       entry_pc==0xdaaau || entry_pc==0xdacau || entry_pc==0xdafcu || entry_pc==0xdac4u || entry_pc==0xda3du || entry_pc==0xdc0eu || entry_pc==0xd93fu)return 1;
    gam4980_set_firmware_hle_enabled(1);
    hits_before = s6502_firmware_hle_hits;
    hle_executed = s6502_exec(&sys.cpu, cycles);
    if (hle_executed != cycles ||
        s6502_firmware_hle_hits != hits_before + 1u ||
        !cpu_equal(&sys.cpu, &reference_cpu) ||
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) != 0) {
        fprintf(stderr,
            "HLE mismatch case=%lu entry=%04x cycles=%u executed=%lu "
            "hits=%lu/%lu\n"
            "reference pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
            "HLE       pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
            (unsigned long)case_id, entry_pc, cycles,
            (unsigned long)hle_executed, (unsigned long)hits_before,
            (unsigned long)s6502_firmware_hle_hits,
            reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
            reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
            sys.cpu.pc, sys.cpu.ac, sys.cpu.ix, sys.cpu.iy, sys.cpu.sp,
            sys.cpu.status);
        return 0;
    }
    return 1;
}

static int run_load_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t temp = (uint16_t)(0x0300u + next_random() % 0x7cf0u);
    uint32_t index;

    for (index = 0u; index < 0x0300u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x2au] = (uint8_t)temp;
    sys.ram[0x2bu] = (uint8_t)(temp >> 8);
    sys.cpu.pc = (uint16_t)(0xd586u+16u*(case_id&3u));
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 3u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    return compare_hle(
        sys.cpu.pc, 31u, return_pc, case_id, initial_ram, reference_ram);
}

static int run_and_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t left = (uint16_t)(0x0300u + next_random() % 0x7cfcu);
    uint16_t right = (uint16_t)(0x0300u + next_random() % 0x7cfcu);
    uint16_t output = (uint16_t)(0x0300u + next_random() % 0x7cf4u);
    uint16_t cycles = 123u;
    uint32_t index;

    for (index = 0u; index < 0x8000u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x20u] = (uint8_t)left;
    sys.ram[0x21u] = (uint8_t)(left >> 8);
    sys.ram[0x23u] = (uint8_t)right;
    sys.ram[0x24u] = (uint8_t)(right >> 8);
    sys.ram[0x2au] = (uint8_t)output;
    sys.ram[0x2bu] = (uint8_t)(output >> 8);
    for (index = 1u; index < 4u; ++index) {
        cycles = (uint16_t)(cycles +
            (((left & 0xffu) + index) > 0xffu) +
            (((right & 0xffu) + index) > 0xffu));
    }
    {static const uint16_t entries[]={0xd2ca,0xd29d,0xd8bd,0xdb2f,0xddb8,0xd780,0xd7b4,0xd7f1};
     sys.cpu.pc=entries[case_id%8u];}
    if(sys.cpu.pc==0xd29du || sys.cpu.pc==0xdb2fu)cycles+=2u;
    if(sys.cpu.pc==0xd780u || sys.cpu.pc==0xd7b4u || sys.cpu.pc==0xd7f1u){
        cycles=sys.cpu.pc==0xd780u?105u:sys.cpu.pc==0xd7f1u?111u:113u;
        for(index=1;index<4;++index)cycles+=((left&255u)+index)>255u;
    }
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 7u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    return compare_hle(
        sys.cpu.pc, cycles, return_pc, case_id, initial_ram, reference_ram);
}

static int run_compare_long_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t left = (uint16_t)(0x0400u + next_random() % 0x0bfcu);
    uint16_t right = (uint16_t)(0x0400u + next_random() % 0x0bfcu);
    uint16_t cycles = 0u;
    uint16_t nonzero = 0u;
    uint16_t difference;
    uint8_t carry = 1u;
    uint32_t index;

    for (index = 0u; index < 0x5000u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x20u] = (uint8_t)left;
    sys.ram[0x21u] = (uint8_t)(left >> 8);
    sys.ram[0x23u] = (uint8_t)right;
    sys.ram[0x24u] = (uint8_t)(right >> 8);
    for (index = 0u; index < 4u; ++index) {
        difference = (uint16_t)(sys.ram[left + index] +
            (uint8_t)~sys.ram[right + index] + carry);
        carry = difference > 0xffu;
        nonzero += (uint8_t)difference != 0u;
        if (index) {
            cycles = (uint16_t)(cycles +
                (((left & 0xffu) + index) > 0xffu) +
                (((right & 0xffu) + index) > 0xffu));
        }
    }
    cycles = (uint16_t)(cycles + (nonzero ? 92u : 91u) + 8u * nonzero);
    sys.cpu.pc = 0xd362u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 3u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    if (!compare_hle(
            0xd362u, cycles, return_pc, case_id,
            initial_ram, reference_ram)) {
        fprintf(stderr,
            "compare-long operands left=%04x right=%04x "
            "L=%02x%02x%02x%02x R=%02x%02x%02x%02x "
            "nonzero=%u cycles=%u\n",
            left, right,
            initial_ram[left], initial_ram[left + 1u],
            initial_ram[left + 2u], initial_ram[left + 3u],
            initial_ram[right], initial_ram[right + 1u],
            initial_ram[right + 2u], initial_ram[right + 3u],
            nonzero, cycles);
        return 0;
    }
    return 1;
}

static int run_indirect_call_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t target_pc = (uint16_t)next_random();
    uint16_t caller_pc = (uint16_t)next_random();
    uint32_t index;

    for (index = 0u; index < 0x0300u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x26u] = (uint8_t)target_pc;
    sys.ram[0x27u] = (uint8_t)(target_pc >> 8);
    sys.cpu.pc = 0xd572u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 3u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(caller_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((caller_pc - 1u) >> 8);
    return compare_hle(
        0xd572u, 37u, target_pc, case_id, initial_ram, reference_ram);
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id;
    int result = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s 8.BIN E.BIN\n", argv[0]);
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (uint8_t *)malloc(GAM4980_FLASH_SIZE);
    buffers.rom_8 = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_e = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    initial_ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    reference_ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e ||
        !initial_ram || !reference_ram ||
        !load_exact(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], buffers.rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0) {
        fprintf(stderr, "could not initialize exact ROM test environment\n");
        result = 2;
        goto cleanup;
    }
    gam4980_set_performance_debug(1);
    /* Keep the reference side in the instruction interpreter.  The guarded
     * HLE hooks run before AOT signature lookup, so they remain testable while
     * avoiding an AOT superblock's intentional end-of-slice overshoot. */
    memset(s6502_aot_validation, 2, sizeof(s6502_aot_validation));
    for (case_id = 0u; case_id < TEST_CASES; ++case_id) {
        if (!run_load_case(case_id, initial_ram, reference_ram) ||
            !run_and_case(case_id, initial_ram, reference_ram) ||
            !run_compare_long_case(case_id, initial_ram, reference_ram) ||
            !run_indirect_call_case(case_id, initial_ram, reference_ram))
            goto cleanup_core;
        sys.cpu.pc=0xd340u;sys.cpu.sp=(uint8_t)case_id;
        sys.cpu.status=(uint8_t)(next_random()&~8u);
        sys.ram[0x20]=(uint8_t)next_random();sys.ram[0x21]=(uint8_t)next_random();
        sys.ram[0x23]=(uint8_t)next_random();sys.ram[0x24]=(uint8_t)next_random();
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43u;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44u;
        if(!compare_hle(0xd340u,s6502_firmware_hle_compare_cycles(),0x4444u,
            case_id,initial_ram,reference_ram))goto cleanup_core;
        sys.cpu.pc=(case_id&1u)?0xd7a6u:0xd7e1u;
        sys.cpu.sp=(uint8_t)case_id;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43u;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44u;
        if(!compare_hle(sys.cpu.pc,(case_id&1u)?24u:31u,0x4444u,
            case_id,initial_ram,reference_ram))goto cleanup_core;
        {
            static const uint16_t entries[]={0xda09,0xdbe1,0xdda7,0xdde4,0xddee,0xd8e9};
            uint16_t cost,value;unsigned count;
            sys.cpu.pc=entries[case_id%6u];sys.cpu.sp=(uint8_t)case_id;
            sys.cpu.ac=(uint8_t)((case_id&4u)?next_random():0u);
            sys.cpu.status=(uint8_t)(next_random()&~8u);
            sys.ram[0x23]=(uint8_t)(case_id%17u);
            sys.ram[0x20]=(uint8_t)((case_id&4u)?next_random():0u);
            sys.ram[0x21]=(uint8_t)((case_id&4u)?next_random():0u);
            count=sys.ram[0x23];value=mem_read16(0x20);
            cost=sys.cpu.pc==0xdda7u?30u:sys.cpu.pc==0xdde4u?(sys.cpu.ac?13u:12u):
                sys.cpu.pc==0xddeeu?(value?25u:27u):(!count?11u:count>=8u?18u:16u+7u*count);
            if(sys.cpu.pc==0xd8e9u)cost=!count?11u:count>=8u?((sys.cpu.ac&128u)?23u:22u):
                (sys.cpu.ac&128u)?21u+9u*count:20u+7u*count;
            sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43u;
            sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44u;
            if(!compare_hle(sys.cpu.pc,cost,0x4444u,case_id,initial_ram,reference_ram))
                goto cleanup_core;
            sys.cpu.pc=case_id%3u==0u?0xd90bu:(case_id&1u)?0xda1au:0xdbf2u;
            sys.cpu.sp=(uint8_t)case_id;
            sys.ram[0x23]=(uint8_t)(case_id%19u);
            sys.ram[0x24]=(uint8_t)((case_id&4u)?0u:next_random());
            count=sys.ram[0x23];value=sys.ram[0x24];
            cost=value?((case_id&1u)?19u:20u):!count?((case_id&1u)?17u:16u):
                count>=16u?((case_id&1u)?30u:29u):22u+15u*count;
            if(sys.cpu.pc==0xd90bu){
                unsigned negative=sys.ram[0x21]&128u;
                cost=value?(negative?26u:25u):!count?16u:count>=16u?
                    (negative?35u:34u):negative?27u+17u*count:26u+15u*count;
            }
            sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43u;
            sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44u;
            if(!compare_hle(sys.cpu.pc,cost,0x4444u,case_id,initial_ram,reference_ram))
                goto cleanup_core;
        }
        {
            static const uint16_t entries[]={0xd49e,0xd50c,0xd85f,0xd8ae,0xd85a,0xd8a5};
            uint16_t pointer=(uint16_t)(0x300u+case_id%0x700u),cost;
            sys.cpu.pc=entries[case_id%6u];sys.cpu.sp=(uint8_t)case_id;
            sys.cpu.ac=(uint8_t)next_random();sys.cpu.status=(uint8_t)(next_random()&~8u);
            sys.ram[0x20]=sys.ram[0x23]=(uint8_t)pointer;
            sys.ram[0x21]=sys.ram[0x24]=(uint8_t)(pointer>>8);
            cost=sys.cpu.pc==0xd49eu?((sys.cpu.ac&128u)?18u:17u):
                sys.cpu.pc==0xd50cu?((sys.ram[0x23]&128u)?25u:24u):
                (sys.cpu.pc==0xd85fu?28u:35u)+((pointer&255u)==255u);
            if(sys.cpu.pc==0xd85au || sys.cpu.pc==0xd8a5u)cost=sys.cpu.pc==0xd85au?13u:23u;
            sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43u;
            sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44u;
            if(!compare_hle(sys.cpu.pc,cost,0x4444u,case_id,initial_ram,reference_ram))goto cleanup_core;
        }
    }
    for(case_id=0;case_id<TEST_CASES;++case_id){
        static const uint16_t entries[]={0xd4a9,0xd4c6,0xd519,0xd53a,0xdd1f,0xdd38,0xdd58,0xdd75};
        unsigned kind=case_id%4u,sign,cost,pointer=0x400u+case_id%0x700u;
        sys.cpu.pc=entries[case_id%8u];sys.cpu.sp=(uint8_t)case_id;
        sys.cpu.ac=(uint8_t)next_random();sys.cpu.status=(uint8_t)(next_random()&~8u);
        sys.ram[0x20]=(uint8_t)next_random();sys.ram[0x21]=(uint8_t)next_random();
        sys.ram[0x23]=(uint8_t)next_random();sys.ram[0x24]=(uint8_t)next_random();
        sys.ram[0x2a]=(uint8_t)pointer;sys.ram[0x2b]=(uint8_t)(pointer>>8);
        sign=kind==0?sys.cpu.ac:kind==1?sys.ram[0x21]:kind==2?sys.ram[0x23]:sys.ram[0x24];
        cost=(kind==0?80u:kind==1?86u:kind==2?90u:93u)+((sign&128u)!=0u);
        if(case_id%8u>=4u)cost=kind==0?77u:kind==2?87u:83u;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        if(!compare_hle(sys.cpu.pc,cost,0x4444,case_id,initial_ram,reference_ram))goto cleanup_core;
    }
    for(case_id=0;case_id<TEST_CASES;++case_id){
        unsigned pointer=0x400u+case_id%0x300u,i,cost;
        sys.cpu.pc=0xde02;sys.cpu.sp=(uint8_t)case_id;
        sys.cpu.status=(uint8_t)(next_random()&~8u);
        sys.ram[0x20]=(uint8_t)pointer;sys.ram[0x21]=(uint8_t)(pointer>>8);
        sys.ram[0x2a]=0;sys.ram[0x2b]=8;sys.ram[8]=(uint8_t)case_id;
        for(i=0;i<4;++i)sys.ram[pointer+i]=0;
        if(case_id&1u)sys.ram[pointer]=1;
        cost=case_id&1u?75u:77u;
        for(i=1;i<4;++i)cost+=((pointer&255u)+i)>255u;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        if(!compare_hle(sys.cpu.pc,cost,0x4444,case_id,initial_ram,reference_ram))goto cleanup_core;
    }
    for(case_id=0;case_id<TEST_CASES;++case_id){
        unsigned value=next_random(),cost=case_id&1u?47u:40u;
        sys.cpu.pc=case_id&1u?0xd9f3:0xd9df;sys.cpu.sp=(uint8_t)case_id;
        sys.cpu.status=(uint8_t)(next_random()&~8u);sys.cpu.ac=(uint8_t)next_random();
        sys.ram[0x20]=sys.ram[0x23]=(uint8_t)value;
        sys.ram[0x21]=sys.ram[0x24]=(uint8_t)(value>>8);
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        if(!compare_hle(sys.cpu.pc,cost,0x4444,case_id,initial_ram,reference_ram))goto cleanup_core;
    }
    for(case_id=0;case_id<TEST_CASES;++case_id){
        static const uint16_t entries[]={0xdae6,0xdb19,0xdac1,0xdac7,0xdaaa,0xdaca,0xdafc,0xdac4};
        unsigned kind=case_id%8u,source=0x500u+(case_id&255u),other=source+case_id%7u;
        unsigned cost,i;
        sys.cpu.pc=entries[kind];sys.cpu.sp=(uint8_t)case_id;
        sys.cpu.status=(uint8_t)(next_random()&~8u);sys.cpu.ac=(uint8_t)next_random();
        sys.ram[0x20]=(uint8_t)source;sys.ram[0x21]=(uint8_t)(source>>8);
        sys.ram[0x23]=(uint8_t)other;sys.ram[0x24]=(uint8_t)(other>>8);
        sys.ram[0x28]=0;sys.ram[0x29]=8;
        for(i=0;i<12u;++i)sys.ram[source+i]=(uint8_t)next_random();
        cost=kind<2?58u:kind<4?61u:kind==4?45u:kind==5?55u:kind==6?108u:111u;
        if(kind<4 || kind>=6){
            unsigned pointer=kind==0 || kind==2?other:source;
            for(i=1;i<4;++i)cost+=((pointer&255u)+i)>255u;
        }
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        if(!compare_hle(sys.cpu.pc,cost,0x4444,case_id,initial_ram,reference_ram))goto cleanup_core;
    }
    for(case_id=0;case_id<TEST_CASES;++case_id){
        unsigned source=0x500u+(case_id&255u),counts=0x800u+(case_id&255u),base=0xa00u+(case_id&255u);
        unsigned count=case_id%36u,upper=case_id%8u==0u?128u:0u,cost,i;
        sys.cpu.pc=(case_id/36u)%3u==2u?0xd93f:(case_id/36u)%3u==1u?0xdc0e:0xda3d;sys.cpu.sp=(uint8_t)case_id;
        sys.cpu.status=(uint8_t)(next_random()&~8u);
        sys.ram[0x20]=(uint8_t)source;sys.ram[0x21]=(uint8_t)(source>>8);
        sys.ram[0x23]=(uint8_t)counts;sys.ram[0x24]=(uint8_t)(counts>>8);
        sys.ram[0x2a]=(uint8_t)base;sys.ram[0x2b]=(uint8_t)(base>>8);
        for(i=0;i<4u;++i)sys.ram[source+i]=(uint8_t)next_random();
        sys.ram[counts]=(uint8_t)count;sys.ram[counts+1]=(uint8_t)upper;
        sys.ram[counts+2]=sys.ram[counts+3]=0;
        cost=upper?123u:count>=32u?135u:count?202u+65u*count:165u;
        for(i=1;i<4;++i){cost+=((counts&255u)+i)>255u;
            if(!upper && count<32u)cost+=((source&255u)+i)>255u;}
        if(!upper && count<32u)for(i=0;i<4;++i)cost+=count*(((base&255u)+8u+i)>255u);
        if(sys.cpu.pc==0xd93fu){
            unsigned negative=sys.ram[source+3u]&128u;
            cost+=(negative?10u:12u)+((source&255u)+3u>255u);
            if(!upper && count<32u)cost+=2u;
        }
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        if(!compare_hle(sys.cpu.pc,cost,0x4444,case_id,initial_ram,reference_ram))goto cleanup_core;
    }
    printf("authored firmware runtime: %lu exact-state cases "
           "(address, long binary, compare32, indirect, compare16, negate, byte/boolean) passed\n",
           (unsigned long)(14u*TEST_CASES));
    result = 0;

cleanup_core:
    gam4980_deinit();
cleanup:
    free(reference_ram);
    free(initial_ram);
    free(buffers.rom_e);
    free(buffers.rom_8);
    free(buffers.flash);
    free(buffers.ram);
    return result;
}
