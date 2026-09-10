#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x20u
#include "../src/gam4980_core.c"

typedef struct {
    uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles;
    uintptr_t ram,read8,write8;
} host_multiply_context;
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_multiply_context
#include "../src/firmware_native_multiply.c"
#include "../src/firmware_native_divide.c"
#undef s6502_iram_asm_context_t
#undef FIRMWARE_NATIVE_HOST_TEST

#define TEST_CASES 20000u

static uint32_t random_state = 0x49809288u;
static int byte_mode;
static int alias_mode;
static int signed_mode;
static int modulus_mode;
static int unscale_mode;

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

static int run_case(
    uint16_t multiplicand, uint16_t multiplier, uint32_t case_id,
    uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu;
    s6502_t reference_cpu;
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t cycles;
    uint32_t reference_executed;
    uint32_t hle_executed;
    uint32_t hits_before;
    uint32_t index;

    for (index = 0; index < 0x300u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x20u] = (uint8_t)multiplicand;
    sys.ram[0x21u] = (uint8_t)(multiplicand >> 8);
    sys.ram[0x23u] = (uint8_t)multiplier;
    sys.ram[0x24u] = (uint8_t)(multiplier >> 8);
    sys.cpu.pc = byte_mode == 3 ? 0xd032u : byte_mode == 2 ? 0xd000u : byte_mode ? 0xd184u : 0xd1a2u;
    if(signed_mode)sys.cpu.pc=signed_mode==6?0xd5dc:signed_mode==5?0xd5c6:signed_mode==4?0xd39b:signed_mode==3?0xd65b:signed_mode==2?0xd3ef:0xd6af;
    if(modulus_mode)sys.cpu.pc=byte_mode==3?0xdcb1u:0xdcabu;
    if(unscale_mode)sys.cpu.pc=0xdd8fu;
    sys.cpu.ac = (uint8_t)next_random();
    if(signed_mode>=3)sys.cpu.ac=(uint8_t)multiplicand;
    if(modulus_mode && byte_mode==2)sys.cpu.ac=(uint8_t)multiplicand;
    if(alias_mode){
        sys.cpu.pc=byte_mode==0?0xdcf4u:byte_mode==1?0xdcefu:byte_mode==2?0xdc7bu:0xdc80u;
        if(byte_mode==1 || byte_mode==2){sys.cpu.ac=(uint8_t)multiplicand;sys.ram[0x20]=(uint8_t)next_random();}
    }
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)next_random();
    if(byte_mode==3){sys.cpu.sp=(uint8_t)(5u+sys.cpu.sp%251u);sys.ram[0x2a]=0;sys.ram[0x2b]=4;}
    if(signed_mode==2)sys.cpu.sp=(uint8_t)(9u+sys.cpu.sp%247u);
    if(unscale_mode)sys.cpu.sp=(uint8_t)(9u+sys.cpu.sp%247u);
    if(signed_mode==6)sys.cpu.sp=(uint8_t)(11u+sys.cpu.sp%245u);
    if(modulus_mode && byte_mode==3)sys.cpu.sp=(uint8_t)(7u+sys.cpu.sp%249u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    initial_cpu = sys.cpu;
    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);
    cycles = s6502_firmware_hle_multiply_cycles();
    if (byte_mode) {
        unsigned b = (uint8_t)multiplier, count = 0;
        while (b) { count += b & 1u; b >>= 1; }
        cycles = !(uint8_t)multiplicand ? 12u : !(uint8_t)multiplier ? 17u : 153u + 4u * count;
        if(byte_mode==2){
            unsigned a=(uint8_t)multiplicand,b=(uint8_t)multiplier,q=b?a/b:0;
            cycles=!a?14u:!b?22u:284u;
            if(a&&b)while(q){cycles+=7u*(q&1u);q>>=1;}
        }
        if(byte_mode==3){
            unsigned q=multiplier?multiplicand/multiplier:0;
            cycles=!multiplicand?30u:!multiplier?31u:1132u;
            if(multiplicand&&multiplier)while(q){cycles+=25u*(q&1u);q>>=1;}
        }
    }

    if(signed_mode){
        unsigned na=multiplicand&0x8000u,nb=multiplier&0x8000u;
        unsigned a=na?(uint16_t)(0u-multiplicand):multiplicand,b=nb?(uint16_t)(0u-multiplier):multiplier;
        unsigned bits=b,count=0;while(bits){count+=bits&1u;bits>>=1;}
        cycles=!a?61u:!b?69u:((b&0xff00u)?443u:259u)+19u*count;
        cycles+=nb?(na?117u:116u):(na?83u:22u);
        if(signed_mode==2 || signed_mode==6){
            unsigned q=b?a/b:0u;
            cycles=!a?30u:!b?31u:1132u;
            if(a&&b)while(q){cycles+=25u*(q&1u);q>>=1;}
            cycles+=nb?(na?118u:117u):(na?83u:22u);
            if(signed_mode==6)cycles=!multiplicand?21u:cycles+(na?60u:(multiplicand&0xff00u)?32u:37u);
        }
    }
    if(alias_mode)cycles+=byte_mode==1 || byte_mode==2?6u:3u;
    if(signed_mode>=3 && signed_mode<=5){
        unsigned a=(uint8_t)multiplicand,b=(uint8_t)multiplier,na=a&128u,nb=b&128u,bits;
        if(na)a=(uint8_t)(0u-a);if(nb)b=(uint8_t)(0u-b);
        cycles=!a?12u:!b?17u:153u;
        if(a&&b)for(bits=b;bits;bits>>=1)cycles+=4u*(bits&1u);
        if(signed_mode>=4){
            cycles=!a?14u:!b?22u:284u;
            if(a&&b)for(bits=a/b;bits;bits>>=1)cycles+=7u*(bits&1u);
        }
        cycles+=nb?(na?63u:58u):(na?41u:24u);
        if(signed_mode==5)cycles+=na?28u:23u;
    }
    if(modulus_mode)cycles=byte_mode==2?cycles+21u:!multiplicand?19u:cycles+((multiplicand&0xff00u)?33u:38u);
    if(unscale_mode){unsigned q=multiplicand/3u;cycles=multiplicand?1183u:81u;while(q){cycles+=25u*(q&1u);q>>=1;}}
    gam4980_set_firmware_hle_enabled(0);
    reference_executed = s6502_exec(&sys.cpu, cycles);
    reference_cpu = sys.cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    if (reference_executed != cycles || reference_cpu.pc != return_pc) {
        fprintf(stderr,"initial sp=%02x p=%02x\n",initial_cpu.sp,initial_cpu.status);
        fprintf(
            stderr,
            "reference mismatch case=%lu a=%04x b=%04x cycles=%u "
            "executed=%lu pc=%04x expected_pc=%04x\n",
            (unsigned long)case_id, multiplicand, multiplier, cycles,
            (unsigned long)reference_executed, reference_cpu.pc, return_pc
        );
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    sys.cpu = initial_cpu;
    {
        host_multiply_context c={0};
        c.pc=initial_cpu.pc;c.ac=initial_cpu.ac;c.ix=initial_cpu.ix;
        c.iy=initial_cpu.iy;c.sp=initial_cpu.sp;c.status=initial_cpu.status;
        c.ram=(uintptr_t)sys.ram;c.cycle_budget=cycles;
        c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;
        {
            host_multiply_context saved;
            c.cycles=5;c.cycle_budget=cycles+4u;saved=c;
            if((byte_mode>=2?firmware_native_divide(&c):firmware_native_multiply(&c)) ||
               memcmp(&c,&saved,sizeof(c)) || memcmp(sys.ram,initial_ram,GAM4980_RAM_SIZE)) {
                fprintf(stderr,"arithmetic short-budget mutation pc=%04x\n",initial_cpu.pc);
                return 0;
            }
            c.cycles=0;c.cycle_budget=cycles;
        }
        if((byte_mode>=2?firmware_native_divide(&c):firmware_native_multiply(&c))!=cycles || c.pc!=reference_cpu.pc ||
           c.ac!=reference_cpu.ac || c.ix!=reference_cpu.ix || c.iy!=reference_cpu.iy ||
           c.sp!=reference_cpu.sp || c.status!=reference_cpu.status ||
           memcmp(sys.ram,reference_ram,GAM4980_RAM_SIZE)) {
            fprintf(stderr,"native multiply mismatch %04x * %04x p=%02x/%02x\n",
                multiplicand,multiplier,(unsigned)c.status,reference_cpu.status);
            return 0;
        }
        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    }
    if (byte_mode || alias_mode || signed_mode || modulus_mode) return 1;
    gam4980_set_firmware_hle_enabled(1);
    hits_before = s6502_firmware_hle_hits;
    hle_executed = s6502_exec(&sys.cpu, cycles);
    if (hle_executed != cycles || s6502_firmware_hle_hits != hits_before + 1u ||
        !cpu_equal(&sys.cpu, &reference_cpu) ||
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) != 0) {
        fprintf(
            stderr,
            "HLE mismatch case=%lu a=%04x b=%04x cycles=%u "
            "executed=%lu hits=%lu/%lu\n"
            "reference pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
            "HLE       pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
            (unsigned long)case_id, multiplicand, multiplier, cycles,
            (unsigned long)hle_executed, (unsigned long)hits_before,
            (unsigned long)s6502_firmware_hle_hits,
            reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
            reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
            sys.cpu.pc, sys.cpu.ac, sys.cpu.ix, sys.cpu.iy, sys.cpu.sp,
            sys.cpu.status
        );
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const uint16_t edge_values[] = {
        0x0000u, 0x0001u, 0x0002u, 0x007fu, 0x0080u, 0x00ffu,
        0x0100u, 0x7fffu, 0x8000u, 0xff00u, 0xffffu,
    };
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id = 0u;
    size_t left;
    size_t right;
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
    if (sys.bk_tab[0x0du] != 0x0ea8u) {
        fprintf(stderr, "unexpected firmware bank D=%03x\n", sys.bk_tab[0x0du]);
        result = 2;
        goto cleanup_core;
    }
    gam4980_set_performance_debug(1);
    memset(s6502_aot_validation,2,sizeof(s6502_aot_validation));

    for (left = 0; left < sizeof(edge_values) / sizeof(edge_values[0]); ++left) {
        for (right = 0; right < sizeof(edge_values) / sizeof(edge_values[0]);
             ++right) {
            if (!run_case(
                    edge_values[left], edge_values[right], case_id++,
                    initial_ram, reference_ram)) {
                result = 1;
                goto cleanup_core;
            }
        }
    }
    while (case_id < TEST_CASES) {
        if (!run_case(
                (uint16_t)next_random(), (uint16_t)next_random(), case_id++,
                initial_ram, reference_ram)) {
            result = 1;
            goto cleanup_core;
        }
    }
    for(byte_mode=1;byte_mode<=2;++byte_mode){
    for (left = 0; left < 256; ++left) {
        for (right = 0; right < 256; ++right) {
            if (!run_case((uint16_t)left, (uint16_t)right, case_id++, initial_ram, reference_ram)) {
                result = 1;
                goto cleanup_core;
            }
        }
    }
    }
    byte_mode=3;
    for(left=0;left<TEST_CASES;++left){
        uint16_t a=(uint16_t)next_random(),b=(uint16_t)next_random();
        if(left<11)a=0;if(left>=11 && left<22)b=0;
        if(!run_case(a,b,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    alias_mode=1;
    for(byte_mode=0;byte_mode<4;++byte_mode)for(left=0;left<2000u;++left){
        uint16_t a=(uint16_t)next_random(),b=(uint16_t)next_random();
        if(left<8)a=0;if(left>=8 && left<16)b=0;
        if(!run_case(a,b,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    alias_mode=0;byte_mode=0;signed_mode=1;
    for(left=0;left<TEST_CASES;++left){
        uint16_t a=(uint16_t)next_random(),b=(uint16_t)next_random();
        if(left<8)a=0;if(left>=8 && left<16)b=0;
        if(left>=16 && left<24)a=0x8000;if(left>=24 && left<32)b=0x8000;
        if(!run_case(a,b,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    byte_mode=3;signed_mode=2;
    for(left=0;left<TEST_CASES;++left){
        uint16_t a=(uint16_t)next_random(),b=(uint16_t)next_random();
        if(left<8)a=0;if(left>=8 && left<16)b=0;
        if(left>=16 && left<24)a=0x8000;if(left>=24 && left<32)b=0x8000;
        if(!run_case(a,b,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    signed_mode=3;byte_mode=1;
    for(left=0;left<256u;++left)for(right=0;right<256u;++right){
        if(!run_case(left,right,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    signed_mode=4;byte_mode=2;
    for(left=0;left<256u;++left)for(right=0;right<256u;++right){
        if(!run_case(left,right,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    signed_mode=5;byte_mode=2;
    for(left=0;left<256u;++left)for(right=0;right<256u;++right){
        if(!run_case(left,right,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    signed_mode=6;byte_mode=3;
    for(left=0;left<TEST_CASES;++left){
        uint16_t a=(uint16_t)next_random(),b=(uint16_t)next_random();
        if(left<8)a=0;if(left>=8 && left<16)b=0;
        if(left>=16 && left<24)a=0x8000;if(left>=24 && left<32)b=0x8000;
        if(!run_case(a,b,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    signed_mode=0;modulus_mode=1;
    for(byte_mode=2;byte_mode<=3;++byte_mode)for(left=0;left<TEST_CASES;++left){
        uint16_t a=(uint16_t)next_random(),b=(uint16_t)next_random();
        if(left<8)a=0;if(left>=8 && left<16)b=0;
        if(left>=16 && left<100)a&=255u;
        if(!run_case(a,b,case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    modulus_mode=0;unscale_mode=1;byte_mode=3;
    for(left=0;left<65536u;++left){
        if(!run_case(left,(uint16_t)next_random(),case_id++,initial_ram,reference_ram)){result=1;goto cleanup_core;}
    }
    printf("firmware arithmetic: %lu exact-state cases passed\n",
           (unsigned long)case_id);
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
