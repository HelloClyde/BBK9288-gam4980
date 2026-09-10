#define main bank_switch_test_main
#include "firmware_hle_bank_switch_test.c"
#undef main

static int packaged(uint16_t cycles,uint8_t *initial,uint8_t *reference)
{
    s6502_t start=sys.cpu, expected;
    uint16_t banks[16], expected_banks[16];
    uint8_t selected=sys.bk_sel, expected_selected;
    host_fw_context c={0};
    uint32_t got;
    memcpy(initial,sys.ram,GAM4980_RAM_SIZE);memcpy(banks,sys.bk_tab,sizeof(banks));
    gam4980_set_firmware_hle_enabled(0);
    if(s6502_exec(&sys.cpu,cycles)!=cycles)return 0;
    expected=sys.cpu;expected_selected=sys.bk_sel;
    memcpy(reference,sys.ram,GAM4980_RAM_SIZE);
    memcpy(expected_banks,sys.bk_tab,sizeof(banks));
    memcpy(sys.ram,initial,GAM4980_RAM_SIZE);restore_banks(banks,selected);sys.cpu=start;
    c.pc=start.pc;c.ac=start.ac;c.ix=start.ix;c.iy=start.iy;c.sp=start.sp;c.status=start.status;
    c.ram=(uintptr_t)sys.ram;c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;
    c.cycle_budget=cycles-1u;
    if(firmware_native_bank(&c)!=0u || c.cycles || c.pc!=start.pc ||
       memcmp(initial,sys.ram,GAM4980_RAM_SIZE) || memcmp(banks,sys.bk_tab,sizeof(banks)))
        return 0;
    c.cycle_budget=cycles;
    got=firmware_native_bank(&c);
    if(got!=cycles || c.cycles!=cycles || c.pc!=expected.pc || c.ac!=expected.ac ||
       c.ix!=expected.ix || c.iy!=expected.iy || c.sp!=expected.sp || c.status!=expected.status ||
       sys.bk_sel!=expected_selected || memcmp(reference,sys.ram,GAM4980_RAM_SIZE) ||
       memcmp(expected_banks,sys.bk_tab,sizeof(banks))) {
        fprintf(stderr,"authored module mismatch pc=%04x got=%u expected=%u\n",start.pc,got,cycles);
        return 0;
    }
    memcpy(sys.ram,initial,GAM4980_RAM_SIZE);restore_banks(banks,selected);sys.cpu=start;
    return 1;
}

static int budgets(uint16_t limit, uint8_t *initial, uint8_t *reference)
{
    s6502_t start=sys.cpu, expected;
    uint16_t banks[16], expected_banks[16];
    uint8_t selected=sys.bk_sel, expected_selected;
    unsigned budget, ref_cycles, got;
    memcpy(initial,sys.ram,GAM4980_RAM_SIZE);
    memcpy(banks,sys.bk_tab,sizeof(banks));
    for(budget=1;budget<=limit;++budget) {
        memcpy(sys.ram,initial,GAM4980_RAM_SIZE);restore_banks(banks,selected);sys.cpu=start;
        gam4980_set_firmware_hle_enabled(0);
        ref_cycles=s6502_exec(&sys.cpu,budget);expected=sys.cpu;
        memcpy(reference,sys.ram,GAM4980_RAM_SIZE);
        memcpy(expected_banks,sys.bk_tab,sizeof(banks));expected_selected=sys.bk_sel;
        memcpy(sys.ram,initial,GAM4980_RAM_SIZE);restore_banks(banks,selected);sys.cpu=start;
        gam4980_set_firmware_hle_enabled(1);
        got=s6502_exec(&sys.cpu,budget);
        if(got!=ref_cycles || !cpu_equal(&sys.cpu,&expected) ||
           memcmp(reference,sys.ram,GAM4980_RAM_SIZE) ||
           memcmp(expected_banks,sys.bk_tab,sizeof(banks)) || sys.bk_sel!=expected_selected) {
            fprintf(stderr,"budget mismatch entry=%04x budget=%u\n",start.pc,budget);return 0;
        }
    }
    memcpy(sys.ram,initial,GAM4980_RAM_SIZE);restore_banks(banks,selected);sys.cpu=start;
    return 1;
}

int main(int argc,char **argv)
{
    gam4980_buffers_t b;
    uint8_t *initial,*reference;
    unsigned i,e,j;
    int ok=1;
    if(argc!=3)return 2;
    memset(&b,0,sizeof(b));
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);
    b.flash_size=GAM4980_FLASH_SIZE;
    initial=malloc(GAM4980_RAM_SIZE);reference=malloc(GAM4980_RAM_SIZE);
    if(!b.ram||!b.flash||!b.rom_8||!b.rom_e||!initial||!reference||
       !load_exact(argv[1],b.rom_8,GAM4980_ROM_SIZE)||
       !load_exact(argv[2],b.rom_e,GAM4980_ROM_SIZE)||gam4980_init(&b)<=0)return 2;
    gam4980_set_performance_debug(1);
    for(e=0;e<3&&ok;++e)for(i=0;i<10000&&ok;++i){
        uint16_t p=(uint16_t)(0x400u+(i&255u)),cycles,target=0x4444;
        uint8_t count=(uint8_t)(next_random()%5u),selected=(uint8_t)(5u+next_random()%5u);
        for(j=0;j<0x2100;++j)sys.ram[j]=(uint8_t)next_random();
        sys.ram[_SYSCON]=0;
        sys.cpu.pc=e==2?0xf457:(e?0xf48b:0xf475);sys.cpu.ac=selected;
        sys.cpu.ix=count;sys.cpu.iy=(uint8_t)next_random();
        sys.cpu.sp=(uint8_t)i;sys.cpu.status=(uint8_t)(next_random()&~8u);
        mem_write(0x100u|(uint8_t)(sys.cpu.sp+1u),(uint8_t)(target-1u));
        mem_write(0x100u|(uint8_t)(sys.cpu.sp+2u),(uint8_t)((target-1u)>>8));
        sys.bk_sel=selected;sys.bk_tab[selected]=(uint16_t)(next_random()&0xfffu);mem_bs(selected);
        cycles=(uint16_t)(36u*count+19u);
        if(e==2){
            uint16_t destination=(uint16_t)(0x600u+(i&255u));
            mem_write(0x28,(uint8_t)p);mem_write(0x29,(uint8_t)(p>>8));
            mem_write(p,(uint8_t)destination);
            mem_write((uint16_t)(p+1u),(uint8_t)(destination>>8));
            cycles=(uint16_t)(67u+((p&255u)==255u));
        }else if(e){
            mem_write(0x100u|sys.cpu.sp,sys.cpu.status|0x30u);--sys.cpu.sp;
            mem_write(0x100u|sys.cpu.sp,(uint8_t)next_random());--sys.cpu.sp;
        }else{
            mem_write(0x28,(uint8_t)p);mem_write(0x29,(uint8_t)(p>>8));
            mem_write(p,(uint8_t)(count+1u));
            mem_write((uint16_t)(p+1u),(uint8_t)next_random());
            mem_write((uint16_t)(p+2u),(uint8_t)next_random());
            cycles=(uint16_t)(cycles+42u+((p&255u)==255u)+((p&255u)>=254u));
        }
        if(!packaged(cycles,initial,reference))return 1;
        if(i<16 && !budgets(cycles,initial,reference))return 1;
        ok=compare_hle(sys.cpu.pc,cycles,target,i,initial,reference);
    }
    printf("bank range/get exact CPU/RAM/banks: %s (3 x 10000)\n",ok?"PASS":"FAIL");
    gam4980_deinit();free(initial);free(reference);free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);
    return ok?0:1;
}
