/* Reuse the exact-state bank/ROM comparison harness, not the old test main. */
#define main bank_switch_test_main
#include "firmware_hle_bank_switch_test.c"
#undef main

static int budget_equivalence(uint8_t *initial, uint8_t *reference)
{
    s6502_t start=sys.cpu, expected;
    uint16_t banks[16], expected_banks[16];
    uint8_t selected=sys.bk_sel, expected_selected;
    unsigned budget, ref_cycles, got;
    memcpy(initial,sys.ram,GAM4980_RAM_SIZE);
    memcpy(banks,sys.bk_tab,sizeof(banks));
    for(budget=1;budget<=108;++budget) {
        memcpy(sys.ram,initial,GAM4980_RAM_SIZE); restore_banks(banks,selected); sys.cpu=start;
        gam4980_set_firmware_hle_enabled(0);
        ref_cycles=s6502_exec(&sys.cpu,budget); expected=sys.cpu;
        memcpy(reference,sys.ram,GAM4980_RAM_SIZE);
        memcpy(expected_banks,sys.bk_tab,sizeof(banks)); expected_selected=sys.bk_sel;
        memcpy(sys.ram,initial,GAM4980_RAM_SIZE); restore_banks(banks,selected); sys.cpu=start;
        gam4980_set_firmware_hle_enabled(1);
        got=s6502_exec(&sys.cpu,budget);
        if(got!=ref_cycles || !cpu_equal(&sys.cpu,&expected) ||
           memcmp(reference,sys.ram,GAM4980_RAM_SIZE) ||
           memcmp(expected_banks,sys.bk_tab,sizeof(banks)) || sys.bk_sel!=expected_selected) {
            fprintf(stderr,"budget mismatch %u\n",budget); return 0;
        }
    }
    memcpy(sys.ram,initial,GAM4980_RAM_SIZE); restore_banks(banks,selected); sys.cpu=start;
    return 1;
}

int main(int argc, char **argv)
{
    gam4980_buffers_t b;
    uint8_t *initial, *reference;
    unsigned i, e, j;
    static const uint16_t entries[] = {0xf4a5,0xf4ad,0xf4af};
    int ok = 1;
    if (argc != 3) return 2;
    memset(&b, 0, sizeof(b));
    b.ram = calloc(1, GAM4980_RAM_SIZE);
    b.flash = calloc(1, GAM4980_FLASH_SIZE);
    b.rom_8 = malloc(GAM4980_ROM_SIZE);
    b.rom_e = malloc(GAM4980_ROM_SIZE);
    b.flash_size = GAM4980_FLASH_SIZE;
    initial = malloc(GAM4980_RAM_SIZE);
    reference = malloc(GAM4980_RAM_SIZE);
    if (!b.ram || !b.flash || !b.rom_8 || !b.rom_e || !initial || !reference ||
        !load_exact(argv[1], b.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], b.rom_e, GAM4980_ROM_SIZE) || gam4980_init(&b) <= 0)
        return 2;
    gam4980_set_performance_debug(1);
    for (e=0;e<3 && ok;++e) for (i=0;i<10000 && ok;++i) {
        uint16_t target = (uint16_t)next_random();
        uint16_t consumed = e == 0 ? 0 : e == 1 ? 17 : 20;
        uint16_t cycles;
        for(j=0;j<0x2100;++j) sys.ram[j]=(uint8_t)next_random();
        sys.ram[_SYSCON]=0;
        sys.bk_tab[5]=(uint16_t)(next_random() & 0xfffu);
        mem_bs(5);
        sys.bk_sel=(uint8_t)(next_random() & 15u);
        sys.ram[0x3d5]=(uint8_t)(i & 15u);
        sys.cpu.pc=0xf4a5;
        sys.cpu.ac=(uint8_t)next_random();
        sys.cpu.ix=(uint8_t)next_random();
        sys.cpu.iy=(uint8_t)next_random();
        sys.cpu.sp=(uint8_t)i;
        sys.cpu.status=(uint8_t)(next_random() & ~0x08u);
        sys.ram[0x100u | (uint8_t)(sys.cpu.sp+1)]=(uint8_t)(target-1);
        sys.ram[0x100u | (uint8_t)(sys.cpu.sp+2)]=(uint8_t)((target-1)>>8);
        cycles=(uint16_t)(108 + ((sys.bk_tab[5]>>8)<mem_read(0x3d5) ? 2 : 0));
        if(consumed) {
            /* The C reference only yields at control boundaries. Construct
             * the exact PHP/SEI/TXA/PHA/TYA/PHA/LDA prologue state instead. */
            mem_write(0x100u | sys.cpu.sp,sys.cpu.status | 0x30u); --sys.cpu.sp;
            mem_write(0x100u | sys.cpu.sp,sys.cpu.ix); --sys.cpu.sp;
            mem_write(0x100u | sys.cpu.sp,sys.cpu.iy); --sys.cpu.sp;
            sys.cpu.ac=5;
            sys.cpu.status=(uint8_t)((sys.cpu.status | 4u) & ~0x82u);
            sys.cpu.pc=entries[e];
            if(e==2) mem_write(0x0c,5);
        }
        if(e==0 && i<16 && !budget_equivalence(initial,reference)) return 1;
        ok=compare_hle(entries[e],(uint16_t)(cycles-consumed),target,i,initial,reference);
    }
    printf("bank query exact CPU/RAM/banks: %s (3 x 10000)\n",ok?"PASS":"FAIL");
    gam4980_deinit();
    free(initial); free(reference); free(b.ram); free(b.flash); free(b.rom_8); free(b.rom_e);
    return ok?0:1;
}
