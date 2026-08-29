#include "Dsys.h"
#include "gam4980_types.h"

/* S1C33L05 ID and clock-control registers. This probe never writes MMIO. */
#define REG_CORE_ID       0x00300000u
#define REG_SERIES_ID     0x00300001u
#define REG_MODEL_ID      0x00300002u
#define REG_VERSION       0x00300003u
#define REG_POWER_CTRL    0x00040180u
#define REG_PRESCALER_SEL 0x00040181u
#define REG_SYSCLK_DIV    0x00300f30u
#define REG_PLL_CTRL      0x00300f31u

static char g_result[384];

static u8 read_mmio8(u32 address)
{
    return *(volatile const u8 *)(unsigned long)address;
}

static char *append_text(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

static char *append_u32(char *out, u32 value)
{
    char digits[10];
    unsigned int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count)
        *out++ = digits[--count];
    return out;
}

static char *append_hex8(char *out, u8 value)
{
    static const char hex[] = "0123456789ABCDEF";

    *out++ = hex[(value >> 4) & 15u];
    *out++ = hex[value & 15u];
    return out;
}

T_WORD App_Main(void)
{
    const u8 core = read_mmio8(REG_CORE_ID);
    const u8 series = read_mmio8(REG_SERIES_ID);
    const u8 model = read_mmio8(REG_MODEL_ID);
    const u8 version = read_mmio8(REG_VERSION);
    const u8 power = read_mmio8(REG_POWER_CTRL);
    const u8 prescaler = read_mmio8(REG_PRESCALER_SEL);
    const u8 sysclk = read_mmio8(REG_SYSCLK_DIV);
    const u8 pll = read_mmio8(REG_PLL_CTRL);
    const u32 cpu_div = 1u << ((power >> 6) & 3u);
    const u32 sys_value = sysclk & 3u;
    const u32 sys_div = sys_value ? sys_value : 0u;
    const int high_speed = (power & 4u) != 0;
    const int pll_bypassed = (pll & 2u) != 0;
    const u32 pll_mul = (pll & 1u) ? 2u : 1u;
    char *out = g_result;

    out = append_text(out, "READ ONLY - no clock writes\n");
    out = append_text(out, "ID=");
    out = append_hex8(out, core);
    *out++ = '/';
    out = append_hex8(out, series);
    *out++ = '/';
    out = append_hex8(out, model);
    out = append_text(out, " VER=");
    out = append_hex8(out, version);
    out = append_text(out, "\nPWR=");
    out = append_hex8(out, power);
    out = append_text(out, " PRES=");
    out = append_hex8(out, prescaler);
    out = append_text(out, " SYS=");
    out = append_hex8(out, sysclk);
    out = append_text(out, " PLL=");
    out = append_hex8(out, pll);

    out = append_text(out, "\nchip=");
    if (core == 0x02u && series == 0x15u && model == 0x05u)
        out = append_text(out, "S1C33L05");
    else
        out = append_text(out, "unknown/check raw ID");

    out = append_text(out, "\nsource=");
    out = append_text(out, high_speed ? "OSC3/PLL" : "OSC1");
    out = append_text(out, " cpu_div=");
    out = append_u32(out, cpu_div);

    out = append_text(out, "\npll=");
    if (pll_bypassed) {
        out = append_text(out, "bypass");
    } else {
        out = append_text(out, "used x");
        out = append_u32(out, pll_mul);
        out = append_text(out, " input_div=");
        if (sys_div)
            out = append_u32(out, sys_div);
        else
            out = append_text(out, "reserved");
    }

    out = append_text(out, "\ncalc_if_OSC3_48=");
    if (!high_speed || (!pll_bypassed && !sys_div)) {
        out = append_text(out, "n/a");
    } else {
        u32 mhz_x10;
        if (pll_bypassed)
            mhz_x10 = 480u / cpu_div;
        else
            mhz_x10 = (480u * pll_mul) / (sys_div * cpu_div);
        out = append_u32(out, mhz_x10 / 10u);
        *out++ = '.';
        *out++ = (char)('0' + mhz_x10 % 10u);
        out = append_text(out, " MHz");
    }
    *out = 0;

    (void)fnGUI_MessageBox(
        HWND_DESKTOP, (const T_BYTE *)g_result,
        (const T_BYTE *)"9288 CPU CLOCK", MB_OK
    );
    return 0;
}
