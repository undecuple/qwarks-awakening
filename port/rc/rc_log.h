/* Ratchet & Clank port: the game exe's log calls, a thin wrapper over the real logger (port/common/log/kz3_log.c, from
 * the Killzone 3 sibling port's logger; rc_log_info.c adds the header and end block). Before 2026-11-03 this was a
 * stdio stand-in. The exe builds define KZ3_LOG_REDIRECT_STDIO, so printf/fprintf(stderr)/puts go through the logger's
 * line router too (see port/rc/CMakeLists.txt). */
#pragma once
#include "kz3_log.h"
#include "rc_log_info.h"
#define RC_LOGE(tag, ...) KZ3_LOGE(tag, __VA_ARGS__)
#define RC_LOGW(tag, ...) KZ3_LOGW(tag, __VA_ARGS__)
#define RC_LOGI(tag, ...) KZ3_LOGI(tag, __VA_ARGS__)
#define RC_LOGD(tag, ...) KZ3_LOGD(tag, __VA_ARGS__)
#define RC_LOGT(tag, ...) KZ3_LOGT(tag, __VA_ARGS__)
/* Close the log (the end block is written by the logger's end hook). Exit paths that know their code call
 * rc_log_exit(code, reason) instead. */
static inline void rc_log_shutdown(void) { kz3_log_shutdown(); }
static inline void rc_log_set_thread_label(const char* label) { kz3_log_set_thread_label(label); }
