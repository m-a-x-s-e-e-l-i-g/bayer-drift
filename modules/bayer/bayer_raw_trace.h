/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#ifndef BAYER_RAW_TRACE_H
#define BAYER_RAW_TRACE_H
#include "fx_camera.h"
#define bayer_raw_trace_runtime FX(raw_trace_runtime)
#define bayer_raw_trace_request FX(raw_trace_request)
#define bayer_raw_trace_service FX(raw_trace_service)
#define bayer_raw_trace_cancel FX(raw_trace_cancel)
#define bayer_raw_trace_shutdown FX(raw_trace_shutdown)
#define bayer_raw_trace_status FX(raw_trace_status)
enum {MT_IDLE,MT_QUEUED,MT_ARMED,MT_DONE,MT_FAILED};
enum {MT_OK,MT_NO_PREVIEW,MT_GUARD,MT_PATCH,MT_TIMEOUT,MT_CANCEL,MT_LOG,MT_SHUTDOWN};
enum {MT_RAW_SOURCE,MT_SSS_SOURCE,MT_PREPARE,MT_START,MT_JPEG_SOURCE,MT_PREPARE_JPEG,MT_JOB_GET,MT_JOB_SET,MT_BINDING_ONLY,MT_KIND_COUNT};
typedef struct {
    unsigned phase,error,core_error,error_pc,installed,events,dropped,skipped,max_hook_us;
    unsigned requested_ms,armed_ms,last_event_ms,starts;
    char log[64];
} bayer_raw_trace_report;
/* Opt-in metadata observer, not a capture request or a sensor writer. Hooks
 * remain boot-resident after first installation; disabling stops recording.
 * Normal ML Modules loader only; no dynamic module_load/module_unload support.
 * No unpatch/slot recycling while a core trampoline might still be returning.
 * Power OFF/restart removes all runtime cache hooks. No flash/NVRAM writes. */
/* Install-only safety observers; no event recording, log IO or idle expiry. */
void bayer_raw_trace_runtime(void);
int bayer_raw_trace_request(void);
void bayer_raw_trace_service(void);
void bayer_raw_trace_cancel(void);
void bayer_raw_trace_shutdown(void);
void bayer_raw_trace_status(bayer_raw_trace_report*out);
#endif
