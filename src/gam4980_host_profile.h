#ifndef GAM4980_HOST_PROFILE_H
#define GAM4980_HOST_PROFILE_H
/* Exclusive attribution on sampled execution slices. Nested phases restore
 * their caller; clock-reader overhead remains in the preceding phase (the
 * read count is exposed), never in both NAT and its interpreter caller. */
enum { HP_OFF, HP_CORE, HP_IRAM, HP_NATIVE, HP_GRAPHICS, HP_TEXT,
       HP_FASTCHAIN, HP_IO, HP_INPUT, HP_HLE, HP_SEMANTIC, HP_COUNT };
typedef struct {
    u32 ticks[HP_COUNT], calls[HP_COUNT], last, current, reads, samples;
    u32 (*clock)(void);
} gam4980_host_profile_t;
static gam4980_host_profile_t host_profile;
#define HP_HLE_PATH_COUNT 32u
/* 0..28 match HLE diagnostic IDs; 31 is dispatch/lookup/unclassified work.
 * Counts are sampled hook events, not guest calls or full-session counts. */
#define HP_HLE_LOOKUP 31u
static u32 host_hle_profile[HP_HLE_PATH_COUNT][5]; /* attempts, accepted, condition, budget, ticks */
static u32 host_hle_owner = HP_HLE_LOOKUP;
#define HP_FUNCTION_COUNT 192u
/* Indexed by stable NAT manifest entry, not a transient resident slot. */
static u32 host_function_profile[HP_FUNCTION_COUNT][4]; /* physical PC, sampled attempts, accepted, exclusive ticks */
static u32 host_function_owner = HP_FUNCTION_COUNT;
static u32 host_private_function_attempts[HP_FUNCTION_COUNT];
static u32 host_private_function_accepted[HP_FUNCTION_COUNT];
static u32 host_profile_switch(u32 phase)
{
    u32 previous = host_profile.current, now;
    if (!host_profile.clock) return previous;
    now = host_profile.clock();
    ++host_profile.reads;
    if (previous && previous < HP_COUNT) {
        host_profile.ticks[previous] += now - host_profile.last;
        if (previous == HP_HLE)
            host_hle_profile[host_hle_owner][4] += now - host_profile.last;
        if (host_function_owner < HP_FUNCTION_COUNT &&
            (previous == HP_NATIVE || previous == HP_GRAPHICS || previous == HP_TEXT))
            host_function_profile[host_function_owner][3] += now - host_profile.last;
    }
    host_profile.last = now;
    host_profile.current = phase;
    if (phase && phase < HP_COUNT) ++host_profile.calls[phase];
    return previous;
}
static u32 host_profile_enter(u32 phase)
{
    return host_profile.current ? host_profile_switch(phase) : HP_OFF;
}
static void host_profile_leave(u32 previous)
{
    if (host_profile.current) (void)host_profile_switch(previous);
}
static void host_profile_phase(u32 phase)
{
    if (host_profile.current && host_profile.current != phase)
        (void)host_profile_switch(phase);
    if (phase == HP_CORE || phase == HP_OFF) host_hle_owner = HP_HLE_LOOKUP;
}
static __attribute__((noinline)) void host_hle_select_sampled(u32 id)
{
    if (host_profile.current != HP_HLE) return;
    if (host_hle_owner != id) (void)host_profile_switch(HP_HLE);
    host_hle_owner = id;
}
#define host_hle_select(id) do { if (host_profile.current == HP_HLE) host_hle_select_sampled(id); } while (0)
static __attribute__((noinline, unused)) void host_hle_event_sampled(u32 id, u32 event)
{
    if (host_profile.current != HP_HLE || id >= HP_HLE_LOOKUP || event > 3u) return;
    if (!event) host_hle_select(id);
    ++host_hle_profile[id][event];
    /* Rejected hook work ends here; subsequent searches are not its cost. */
    if (event >= 2u) host_hle_select(HP_HLE_LOOKUP);
}
#define host_hle_event(id, event) do { if (host_profile.current == HP_HLE) host_hle_event_sampled((id), (event)); } while (0)
static __attribute__((unused)) void host_private_profile_begin(u32 index, u32 physical, u32 phase, u32 *frame)
{
    frame[2] = host_function_owner;
    frame[1] = host_profile_enter(phase);
    host_function_owner = HP_FUNCTION_COUNT;
    if (host_profile.current && index < HP_FUNCTION_COUNT) {
        host_function_owner = index;
        host_function_profile[index][0] = physical;
        ++host_function_profile[index][1];
        ++host_private_function_attempts[index];
    }
}
static __attribute__((unused)) void host_private_profile_end(u32 *frame, u32 accepted)
{
    u32 owner = host_function_owner;
    host_profile_leave(frame[1]);
    if (owner < HP_FUNCTION_COUNT && accepted) {
        ++host_function_profile[owner][2];
        ++host_private_function_accepted[owner];
    }
    host_function_owner = frame[2];
}
#endif
