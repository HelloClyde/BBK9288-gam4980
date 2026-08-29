#include "Dsys.h"
#include "gam4980_types.h"
#include "9288_exec_ram_payloads.h"

#define EXEC_BUFFER_SIZE 128u

typedef u32 (*exec_ram_fn)(u32 left, u32 right);

static u8 g_exec_buffer[EXEC_BUFFER_SIZE]
    __attribute__((aligned(16), section(".scratch")));
static char g_result[512];

static char *append_text(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

static char *append_hex32(char *out, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift = 28;

    while (shift >= 0) {
        *out++ = digits[(value >> (u32)shift) & 0x0fu];
        shift -= 4;
    }
    return out;
}

static void copy_payload(const u8 *source, u32 size)
{
    u32 index;

    for (index = 0; index < size; ++index)
        *(volatile u8 *)&g_exec_buffer[index] = source[index];
    __asm__ volatile("" ::: "memory");
}

static u32 call_payload(u32 left, u32 right)
{
    exec_ram_fn function = (exec_ram_fn)(void *)g_exec_buffer;

    __asm__ volatile("" ::: "memory");
    return function(left, right);
}

static void write_result_file(const char *text)
{
    static const char path[] = "a:\\EXEC_RAM.TXT";
    FS_FILE *file = fs_fopen(path, FS_O_WRONLY);
    u32 length = 0u;
    u32 written = 0u;

    if (!file)
        return;
    while (text[length])
        ++length;
    while (written < length) {
        u32 remaining = length - written;
        size_t chunk = remaining > 32u ? 32u : (size_t)remaining;
        size_t result = fs_fwrite(text + written, 1, chunk, file);

        if (!result || result > remaining)
            break;
        written += (u32)result;
    }
    (void)fs_update(file);
    fs_fclose(file);
}

T_WORD App_Main(void)
{
    const u32 left = 0x12345670u;
    const u32 right = 0x01020304u;
    const u32 expected_a = left + right + 7u;
    const u32 expected_b = (left ^ right) + 19u;
    u32 result_a_first;
    u32 result_b;
    u32 result_a_second;
    int payloads_differ = 0;
    int passed;
    u32 index;
    char *out;

    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)
            "Writes native S1C33 code into .scratch, executes it, then "
            "overwrites the same address. Press OK to run.",
        (const T_BYTE *)"EXEC RAM PROBE",
        MB_OK
    );

    if (EXEC_RAM_PAYLOAD_A_SIZE > EXEC_BUFFER_SIZE ||
        EXEC_RAM_PAYLOAD_B_SIZE > EXEC_BUFFER_SIZE) {
        (void)fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)"Generated payload is too large.",
            (const T_BYTE *)"EXEC RAM PROBE", MB_OK
        );
        return -1;
    }
    for (index = 0; index < EXEC_RAM_PAYLOAD_A_SIZE &&
         index < EXEC_RAM_PAYLOAD_B_SIZE; ++index) {
        if (g_exec_ram_payload_a[index] != g_exec_ram_payload_b[index]) {
            payloads_differ = 1;
            break;
        }
    }
    if (EXEC_RAM_PAYLOAD_A_SIZE != EXEC_RAM_PAYLOAD_B_SIZE)
        payloads_differ = 1;

    copy_payload(g_exec_ram_payload_a, EXEC_RAM_PAYLOAD_A_SIZE);
    result_a_first = call_payload(left, right);
    copy_payload(g_exec_ram_payload_b, EXEC_RAM_PAYLOAD_B_SIZE);
    result_b = call_payload(left, right);
    copy_payload(g_exec_ram_payload_a, EXEC_RAM_PAYLOAD_A_SIZE);
    result_a_second = call_payload(left, right);

    passed = payloads_differ && result_a_first == expected_a &&
        result_b == expected_b && result_a_second == expected_a;
    out = g_result;
    out = append_text(out, "buf=");
    out = append_hex32(out, (u32)(void *)g_exec_buffer);
    out = append_text(out, "\nA1=");
    out = append_hex32(out, result_a_first);
    out = append_text(out, result_a_first == expected_a ? " OK\nB =" : " BAD\nB =");
    out = append_hex32(out, result_b);
    out = append_text(out, result_b == expected_b ? " OK\nA2=" : " BAD\nA2=");
    out = append_hex32(out, result_a_second);
    out = append_text(
        out, result_a_second == expected_a ? " OK\ndifferent=" : " BAD\ndifferent="
    );
    *out++ = payloads_differ ? '1' : '0';
    out = append_text(out, "\nresult=");
    out = append_text(out, passed ? "PASS" : "FAIL");
    *out++ = '\n';
    *out = 0;

    write_result_file(g_result);
    (void)fnGUI_MessageBox(
        HWND_DESKTOP, (const T_BYTE *)g_result,
        (const T_BYTE *)"EXEC RAM RESULT", MB_OK
    );
    return passed ? 0 : -2;
}
