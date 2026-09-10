#ifndef FIRMWARE_NATIVE_METRICS_H
#define FIRMWARE_NATIVE_METRICS_H
#ifdef FIRMWARE_NATIVE_HOST_TEST
#define FW_RECORD(c, cost) ((void)0)
#else
/* Existing ABI metrics: count only accepted complete function calls. */
#define FW_RECORD(c, cost) do { \
    uint32_t *m=(uint32_t *)(unsigned long)(c)->native_shared_metrics; \
    if(m && m[9]) { ++m[0]; ++m[1]; m[2]+=(cost); if(!m[6])m[6]=1u; } \
} while(0)
#endif
#endif
