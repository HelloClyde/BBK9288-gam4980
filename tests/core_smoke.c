#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gam4980_core.h"

static u8 *stream_roms[2];
static u32 stream_rom_reads;
static u32 stream_rom_page_bits[2][GAM4980_ROM_SIZE / 0x1000u / 32u];
static u32 runtime_poll_calls;

static void runtime_poll(void *context)
{
    u32 *calls = (u32 *)context;

    ++*calls;
}

static int stream_rom_read(
    void *context, u8 region, u32 offset, u8 *out, u32 size
)
{
    (void)context;
    if (region > GAM4980_ROM_REGION_E || !out ||
        offset > GAM4980_ROM_SIZE || size > GAM4980_ROM_SIZE - offset)
        return 0;
    memcpy(out, stream_roms[region] + offset, size);
    ++stream_rom_reads;
    if (size) {
        u32 page = offset >> 12;
        u32 last_page = (offset + size - 1u) >> 12;

        while (page <= last_page) {
            stream_rom_page_bits[region][page >> 5] |=
                1u << (page & 31u);
            ++page;
        }
    }
    return 1;
}

static int load_file(const char *path, u8 *data, u32 size)
{
    FILE *file = fopen(path, "rb");
    int ok;

    if (!file)
        return 0;
    ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static int load_game(const char *path, u8 *flash)
{
    u8 header[GAM4980_GAME_HEADER_SIZE];
    FILE *file = fopen(path, "rb");
    long size;

    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < (long)GAM4980_GAME_HEADER_SIZE ||
        size > (long)GAM4980_GAME_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(gam4980_game_storage(), 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    if (gam4980_load_game_header(header, (u32)size) <= 0)
        return 0;
    return flash[0x801c] == ((u32)size & 0xffu) &&
        flash[0x801d] == (((u32)size >> 8) & 0xffu) &&
        flash[0x801e] == (((u32)size >> 16) & 0xffu);
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    const u8 *frame;
    const char *frame_count_text;
    u8 *rom_8;
    u8 *rom_e;
    u32 checksum = 2166136261u;
    u32 index;
    unsigned long frame_count = 120;
    int story_input = 0;
    int result;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: core_smoke 8.BIN E.BIN [game.gam]\n");
        return 2;
    }
    frame_count_text = getenv("GAM4980_SMOKE_FRAMES");
    if (frame_count_text && *frame_count_text) {
        char *end = 0;

        frame_count = strtoul(frame_count_text, &end, 10);
        if (!end || *end || frame_count == 0)
            return 2;
    }
    story_input = getenv("GAM4980_SMOKE_STORY") != 0;
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    rom_8 = (u8 *)malloc(GAM4980_ROM_SIZE);
    rom_e = (u8 *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_8 = rom_8;
    buffers.rom_e = rom_e;
    buffers.framebuffer = 0;
    buffers.flash_size = GAM4980_FLASH_SIZE;
    if (!buffers.ram || !buffers.flash || !rom_8 || !rom_e)
        return 3;
    if (!load_file(argv[1], rom_8, GAM4980_ROM_SIZE) ||
        !load_file(argv[2], rom_e, GAM4980_ROM_SIZE))
        return 4;
    if (getenv("GAM4980_STREAM_ROM")) {
        stream_roms[GAM4980_ROM_REGION_8] = rom_8;
        stream_roms[GAM4980_ROM_REGION_E] = rom_e;
        buffers.rom_8 = 0;
        buffers.rom_e = 0;
        buffers.rom_read = stream_rom_read;
    }
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    gam4980_set_game_load_aot_enabled(
        getenv("GAM4980_DISABLE_GAME_LOAD_AOT") == 0
    );
    {
        const char *semantic_mask = getenv("GAM4980_GAME_AOT_SEMANTIC_MASK");

        if (semantic_mask && *semantic_mask)
            gam4980_set_game_aot_semantic_mask(
                (u32)strtoul(semantic_mask, 0, 0)
            );
        semantic_mask = getenv("GAM4980_GAME_AOT_ENTRY_LIMIT");
        if (semantic_mask && *semantic_mask)
            gam4980_set_game_aot_entry_limit(
                (u32)strtoul(semantic_mask, 0, 0)
            );
        gam4980_set_game_aot_direct_links(
            getenv("GAM4980_DISABLE_GAME_AOT_DIRECT_LINKS") == 0
        );
    }
#endif
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    gam4980_set_firmware_hle_enabled(
        getenv("GAM4980_DISABLE_FIRMWARE_HLE") == 0
    );
#endif
#if (defined(GAM4980_ENABLE_AOT) && defined(GAM4980_AOT_DIAGNOSTICS)) || \
    defined(GAM4980_RUNTIME_PERFORMANCE_LOG) || \
    defined(GAM4980_ENABLE_FIRMWARE_HLE)
    gam4980_set_performance_debug(
        getenv("GAM4980_DISABLE_PERFORMANCE_DEBUG") == 0
    );
#endif
    result = gam4980_init(&buffers);
    if (result <= 0) {
        fprintf(stderr, "gam4980_init failed: %d\n", result);
        return 5;
    }
    if (getenv("GAM4980_SMOKE_RUNTIME_POLL"))
        gam4980_set_runtime_poll_callback(
            runtime_poll, &runtime_poll_calls, 4096u
        );
    if (getenv("GAM4980_SMOKE_WARM_ROM") &&
        !gam4980_warm_bare_rom_cache()) {
        fprintf(stderr, "gam4980_warm_bare_rom_cache failed\n");
        return 8;
    }
    if (getenv("GAM4980_SMOKE_WARM_ROM") &&
        getenv("GAM4980_STREAM_ROM") &&
        gam4980_rom_cache_warm_pages() != 92u) {
        fprintf(
            stderr, "unexpected warm ROM page count: %u (expected 92)\n",
            (unsigned)gam4980_rom_cache_warm_pages()
        );
        return 8;
    }
    if (argc == 4) {
        unsigned long frame_number;
        const char *raw_exec_text;

        if (!load_game(argv[3], buffers.flash))
            return 6;
#ifdef GAM4980_IRAM_EXEC_NATIVE_TEST
        gam4980_set_iram_exec_enabled(
            getenv("GAM4980_SMOKE_IRAM_RESIDENT") != 0
        );
#endif
        raw_exec_text = getenv("GAM4980_SMOKE_RAW_EXECS");
#ifdef GAM4980_IRAM_EXEC_NATIVE_TEST
        if (raw_exec_text && *raw_exec_text) {
            unsigned long raw_execs = strtoul(raw_exec_text, 0, 10);

            for (frame_number = 0; frame_number < raw_execs; ++frame_number)
                (void)gam4980_debug_exec_slice(1u);
        } else
#else
        (void)raw_exec_text;
#endif
        for (frame_number = 0; frame_number < frame_count; ++frame_number) {
            if (story_input) {
                if (frame_number == 3300 || frame_number == 3480 ||
                    frame_number == 4380)
                    gam4980_key_down(GAM4980_KEY_ENTER);
            }
            gam4980_run_frame();
        }
        if (getenv("GAM4980_SMOKE_RUNTIME_POLL")) {
            gam4980_set_runtime_poll_callback(0, 0, 0u);
            if (runtime_poll_calls <= frame_count) {
                fprintf(
                    stderr,
                    "runtime poll did not split frames: calls=%u frames=%lu\n",
                    (unsigned)runtime_poll_calls, frame_count
                );
                return 9;
            }
            printf(
                "runtime poll calls=%u frames=%lu\n",
                (unsigned)runtime_poll_calls, frame_count
            );
        }
    } else {
        (void)gam4980_render_frame();
    }
    frame = gam4980_packed_frame();
    if (!frame)
        return 7;
    for (index = 0; index < GAM4980_LCD_PACKED_SIZE; ++index) {
        checksum ^= frame[index];
        checksum *= 16777619u;
    }
    printf(
        "core initialized: packed=%u bytes fnv1a=%08x halted=%d rom_reads=%u\n",
        (unsigned)GAM4980_LCD_PACKED_SIZE, (unsigned)checksum,
        gam4980_cpu_halted(), (unsigned)stream_rom_reads
    );
    if (getenv("GAM4980_STREAM_ROM")) {
        u32 region;

        for (region = 0u; region < 2u; ++region) {
            u32 pages = 0u;
            u32 word;

            for (word = 0u;
                 word < GAM4980_ROM_SIZE / 0x1000u / 32u; ++word) {
                u32 bits = stream_rom_page_bits[region][word];

                while (bits) {
                    bits &= bits - 1u;
                    ++pages;
                }
            }
            printf(
                "stream rom region=%u unique_pages=%u\n",
                (unsigned)region, (unsigned)pages
            );
            if (getenv("GAM4980_STREAM_ROM_PAGES")) {
                u32 page;

                printf("stream rom region=%u pages=", (unsigned)region);
                for (page = 0u; page < GAM4980_ROM_SIZE / 0x1000u;
                     ++page) {
                    if (stream_rom_page_bits[region][page >> 5] &
                        (1u << (page & 31u)))
                        printf("%x,", (unsigned)page);
                }
                putchar('\n');
            }
        }
    }
    if (getenv("GAM4980_SMOKE_ROM_MISS_TRACE")) {
        u32 miss_id;

        printf(
            "rom miss trace count=%u dropped=%u runtime=%u\n",
            (unsigned)gam4980_rom_miss_trace_count(),
            (unsigned)gam4980_rom_miss_trace_dropped(),
            (unsigned)gam4980_rom_cache_runtime_misses()
        );
        for (miss_id = 0u; miss_id < gam4980_rom_miss_trace_count();
             ++miss_id) {
            printf(
                "rom miss id=%u kind=%u slot=%u region=%u page=%03x\n",
                (unsigned)miss_id,
                (unsigned)gam4980_rom_miss_trace_kind(miss_id),
                (unsigned)gam4980_rom_miss_trace_slot(miss_id),
                (unsigned)gam4980_rom_miss_trace_region(miss_id),
                (unsigned)gam4980_rom_miss_trace_page(miss_id)
            );
        }
    }
#if defined(GAM4980_ENABLE_AOT) && defined(GAM4980_AOT_DIAGNOSTICS)
    printf(
        "aot instructions=%llu\n",
        (unsigned long long)gam4980_aot_instruction_count()
    );
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    printf(
        "game aot entries=%u semantic=%u reachable=%u linked_calls=%u "
        "direct_link_available=%d direct_link_hits=%u code_size=%u "
        "instructions=%llu enabled=%d "
        "hle_matches=%u\n",
        (unsigned)gam4980_game_aot_entry_count(),
        (unsigned)gam4980_game_aot_semantic_count(),
        (unsigned)gam4980_game_aot_reachable_count(),
        (unsigned)gam4980_game_aot_linked_call_count(),
        gam4980_game_aot_direct_link_available(),
        (unsigned)gam4980_game_aot_direct_link_hits(),
        (unsigned)gam4980_game_aot_code_size(),
        (unsigned long long)gam4980_game_aot_instruction_count(),
        gam4980_game_aot_enabled(),
        (unsigned)gam4980_game_hle_match_count()
    );
    printf(
        "direct link stages eligible=%u signature=%u metadata=%u "
        "mapping=%u budget=%u\n",
        (unsigned)gam4980_game_aot_direct_link_stage_hits(0u),
        (unsigned)gam4980_game_aot_direct_link_stage_hits(1u),
        (unsigned)gam4980_game_aot_direct_link_stage_hits(2u),
        (unsigned)gam4980_game_aot_direct_link_stage_hits(3u),
        (unsigned)gam4980_game_aot_direct_link_stage_hits(4u)
    );
    {
        u64 pattern_hits[33] = {0};
        u32 entry_id;

        for (entry_id = 0u;
             entry_id < gam4980_game_aot_entry_count(); ++entry_id) {
            u32 pattern = gam4980_game_aot_entry_pattern(entry_id);

            if (pattern < 33u)
                pattern_hits[pattern] +=
                    gam4980_game_aot_entry_hit_count(entry_id);
        }
        for (entry_id = 0u; entry_id < 33u; ++entry_id) {
            if (pattern_hits[entry_id])
                printf(
                    "game aot pattern=%u hits=%llu\n", (unsigned)entry_id,
                    (unsigned long long)pattern_hits[entry_id]
                );
        }
        printf(
            "game aot trace entries=%u hits=%u instructions=%u\n",
            (unsigned)gam4980_game_aot_trace_entry_count(),
            (unsigned)gam4980_game_aot_trace_hits(),
            (unsigned)gam4980_game_aot_trace_instruction_hits()
        );
        if (getenv("GAM4980_SMOKE_AOT_ENTRIES")) {
            for (entry_id = 0u;
                 entry_id < gam4980_game_aot_entry_count(); ++entry_id) {
                u64 hits = gam4980_game_aot_entry_hit_count(entry_id);

                if (hits)
                    printf(
                        "game aot entry=%u pc=%06x pattern=%u hits=%llu\n",
                        (unsigned)entry_id,
                        (unsigned)gam4980_game_aot_entry_physical_pc(entry_id),
                        (unsigned)gam4980_game_aot_entry_pattern(entry_id),
                        (unsigned long long)hits
                    );
            }
        }
    }
#endif
#endif
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    printf(
        "runtime perf calls=%u cycles=%llu samples=%u dropped=%u\n",
        (unsigned)gam4980_performance_exec_calls(),
        (unsigned long long)gam4980_performance_guest_cycles(),
        (unsigned)gam4980_performance_sample_count(),
        (unsigned)gam4980_performance_sample_dropped()
    );
#endif
#ifdef GAM4980_ENABLE_IRAM_EXEC_ENGINE
    {
        u32 burst_total = 0u;
        u32 dispatch_total = 0u;
        u32 slow_opcode_total = 0u;
        u32 slow_class_total = 0u;
        u32 sampled_total;
        u32 expected_samples;
        u32 metric;

        for (metric = 0u; metric < GAM4980_IRAM_BURST_BUCKET_COUNT;
             ++metric)
            burst_total += gam4980_iram_burst_bucket_hits(metric);
        for (metric = 0u; metric < GAM4980_IRAM_DISPATCH_TARGET_COUNT;
             ++metric)
            dispatch_total += gam4980_iram_dispatch_target_hits(metric);
        for (metric = 0u; metric < 256u; ++metric)
            slow_opcode_total += gam4980_iram_slow_opcode_hits(metric);
        for (metric = 0u; metric < GAM4980_IRAM_SLOW_PATH_CLASS_COUNT;
             ++metric)
            slow_class_total += gam4980_iram_slow_path_class_hits(metric);
        sampled_total = dispatch_total + slow_opcode_total;
        expected_samples = (
            gam4980_iram_exec_dispatch_exits() +
            gam4980_iram_exec_zero_fallbacks() +
            gam4980_iram_exec_slow_exits()
        ) / gam4980_iram_exit_sample_rate();
        if ((!getenv("GAM4980_DISABLE_PERFORMANCE_DEBUG") &&
             (burst_total != gam4980_iram_exec_calls() ||
              sampled_total != gam4980_iram_exit_samples() ||
              sampled_total != expected_samples ||
              slow_class_total != slow_opcode_total)) ||
            (getenv("GAM4980_DISABLE_PERFORMANCE_DEBUG") &&
             (burst_total || dispatch_total || slow_opcode_total ||
              slow_class_total || gam4980_iram_exit_samples()))) {
            fprintf(
                stderr,
                "IRAM diagnostics mismatch: bursts=%u calls=%u "
                "dispatch=%u/%u slow=%u/%u classes=%u "
                "samples=%u/%u rate=%u debug_off=%u\n",
                (unsigned)burst_total,
                (unsigned)gam4980_iram_exec_calls(),
                (unsigned)dispatch_total,
                (unsigned)gam4980_iram_exec_dispatch_exits(),
                (unsigned)slow_opcode_total,
                (unsigned)(gam4980_iram_exec_zero_fallbacks() +
                    gam4980_iram_exec_slow_exits()),
                (unsigned)slow_class_total,
                (unsigned)gam4980_iram_exit_samples(),
                (unsigned)expected_samples,
                (unsigned)gam4980_iram_exit_sample_rate(),
                getenv("GAM4980_DISABLE_PERFORMANCE_DEBUG") != 0
            );
            return 9;
        }
    }
    printf(
        "iram exec calls=%u instructions=%u cycles=%u zero=%u "
        "controls=%u deadline=%u dispatch=%u slow=%u max=%u\n",
        (unsigned)gam4980_iram_exec_calls(),
        (unsigned)gam4980_iram_exec_instructions(),
        (unsigned)gam4980_iram_exec_cycles(),
        (unsigned)gam4980_iram_exec_zero_fallbacks(),
        (unsigned)gam4980_iram_exec_control_exits(),
        (unsigned)gam4980_iram_exec_deadline_exits(),
        (unsigned)gam4980_iram_exec_dispatch_exits(),
        (unsigned)gam4980_iram_exec_slow_exits(),
        (unsigned)gam4980_iram_exec_max_instructions()
    );
    printf(
        "iram fastchain calls=%u cycles=%u reentries=%u zero=%u "
        "reinstall_failures=%u\n",
        (unsigned)gam4980_iram_fastchain_calls(),
        (unsigned)gam4980_iram_fastchain_cycles(),
        (unsigned)gam4980_iram_fastchain_reentries(),
        (unsigned)gam4980_iram_fastchain_zero_returns(),
        (unsigned)gam4980_iram_fastchain_reinstall_failures()
    );
#ifdef GAM4980_ENABLE_AOT
    printf(
        "aot token hits=%u",
        (unsigned)gam4980_aot_token_link_hits()
    );
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    printf(
        " game_linear_links=%u game_linear_hits=%u runtime_lifts=%u",
        (unsigned)gam4980_game_aot_linear_link_count(),
        (unsigned)gam4980_game_aot_linear_link_hits(),
        (unsigned)gam4980_game_aot_runtime_lift_hits()
    );
#endif
    printf("\n");
#endif
#ifdef GAM4980_IRAM_EXEC_NATIVE_TEST
    printf(
        "debug cpu pc=%04x regs=%08x status=%02x\n",
        (unsigned)gam4980_debug_cpu_pc(),
        (unsigned)gam4980_debug_cpu_regs(),
        (unsigned)gam4980_debug_cpu_status()
    );
    if (getenv("GAM4980_SMOKE_DUMP_PC")) {
        u32 debug_pc = gam4980_debug_cpu_pc();
        u32 debug_index;

        printf("debug bytes");
        for (debug_index = 0u; debug_index < 48u; ++debug_index)
            printf(
                " %02x", (unsigned)gam4980_debug_read8(
                    (debug_pc + debug_index) & 0xffffu
                )
            );
        printf("\n");
        printf(
            "debug mem 000e=%02x 03d5=%02x 03d6=%02x\n",
            (unsigned)gam4980_debug_read8(0x000eu),
            (unsigned)gam4980_debug_read8(0x03d5u),
            (unsigned)gam4980_debug_read8(0x03d6u)
        );
    }
#endif
#endif
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    {
        u32 path_id;

    printf(
        "firmware hle enabled=%d hits=%u cycles=%llu\n",
        gam4980_firmware_hle_enabled(),
        (unsigned)gam4980_firmware_hle_hits(),
        (unsigned long long)gam4980_firmware_hle_guest_cycles()
    );
    printf(
        "resource span cache hits=%u misses=%u\n",
        (unsigned)gam4980_resource_span_cache_hits(),
        (unsigned)gam4980_resource_span_cache_misses()
    );
        for (path_id = 0;
             path_id < gam4980_firmware_hle_path_count(); ++path_id) {
            printf(
                "hle path=%u pc=%04x attempts=%u hits=%u cond=%u budget=%u "
                "batch_groups=%u batch_iterations=%u batch_max=%u "
                "direct_groups=%u direct_iterations=%u "
                "cycles=%llu\n",
                (unsigned)path_id,
                (unsigned)gam4980_firmware_hle_path_pc(path_id),
                (unsigned)gam4980_firmware_hle_path_attempts(path_id),
                (unsigned)gam4980_firmware_hle_path_hits(path_id),
                (unsigned)gam4980_firmware_hle_path_condition_rejects(path_id),
                (unsigned)gam4980_firmware_hle_path_budget_rejects(path_id),
                (unsigned)gam4980_firmware_hle_path_batch_groups(path_id),
                (unsigned)gam4980_firmware_hle_path_batch_iterations(path_id),
                (unsigned)gam4980_firmware_hle_path_batch_max(path_id),
                (unsigned)gam4980_firmware_hle_path_direct_groups(path_id),
                (unsigned)
                    gam4980_firmware_hle_path_direct_iterations(path_id),
                (unsigned long long)
                    gam4980_firmware_hle_path_guest_cycles(path_id)
            );
        }
    }
#endif
#ifdef GAM4980_STATE_DIAGNOSTICS
    printf(
        "state hash=%016llx\n",
        (unsigned long long)gam4980_state_hash()
    );
    printf(
        "state cpu=%016llx ram=%016llx timing=%016llx\n",
        (unsigned long long)gam4980_state_cpu_hash(),
        (unsigned long long)gam4980_state_ram_hash(),
        (unsigned long long)gam4980_state_timing_hash()
    );
#endif
    gam4980_deinit();
    free(buffers.ram);
    free(buffers.flash);
    free(rom_8);
    free(rom_e);
    return 0;
}
