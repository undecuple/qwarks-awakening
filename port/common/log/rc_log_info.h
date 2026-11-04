/* Ratchet & Clank port: the run header and end block on top of the logger (kz3_log.c, from the Killzone 3 sibling port).
 * Portable C11 (MinGW-w64 and Linux).
 *
 *   rc_log_open   : picks the folder (RC_LOG_DIR, else the exe's folder), names the game, starts the logger and writes
 *                   the header block once: product + port version + commit, game id and title, title ID, update
 *                   version expected, exe SHA-256 (short; a background thread, one line when ready), OS (Windows
 *                   version; Wine/Proton by ntdll's wine_get_version), CPU name and threads, RAM.
 *   rc_log_note_env : every set RC_* / PS3_* / KZ3_* / RSX_* / GCM_* / LD_* variable (names and values; redacted by the
 *                   logger at write time). Call after the exe has set its own defaults.
 *   rc_log_note_sfo : the game's PARAM.SFO fields (TITLE_ID, VERSION, APP_VER, TITLE) as one line, when the boot code has
 *                   the path.
 *   rc_log_exit   : records exit code and reason, then kz3_log_shutdown(), whose end hook writes the end block (exit
 *                   code, reason, run time). A shutdown without it (the runtime's sys_process_exit, patch 2030) still
 *                   writes the end block, with "code not recorded".
 */
#ifndef RC_LOG_INFO_H
#define RC_LOG_INFO_H

#include "kz3_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* product;         /* "Qwark's Awakening" */
    const char* version;         /* port version, "0.1.0" */
    const char* commit;          /* git short hash at configure time, or NULL/"" */
    const char* game_id;         /* catalogue id: tods, acit, ... (names the folder and the files) */
    const char* title;           /* "Ratchet & Clank Future: Tools of Destruction" */
    const char* title_id;        /* "BCUS98127" (the US disc id of the catalogue) */
    const char* update_version;  /* catalogue update version ("01.20"), NULL = none */
} rc_run_info;

/* flags: KZ3_LOG_F_* (0 for the game). Returns kz3_log_init's result (0 ok, 1 no file, -1 already open). */
int  rc_log_open(const rc_run_info* ri, unsigned flags);
void rc_log_note_env(void);
void rc_log_note_sfo(const char* param_sfo_path);
void rc_log_exit(int code, const char* reason);

/* Helpers (also used by the host test). hex64: 65 bytes. Returns 0 on success. */
int  rc_log_sha256_file(const char* path, char hex64[65], unsigned long long* size_out);
/* Folder of the running exe into out (no trailing slash); 0 on success. */
int  rc_log_exe_dir(char* out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
