#ifndef PAYLOAD_DYLIB_CRASHREPORTER_H
#define PAYLOAD_DYLIB_CRASHREPORTER_H

#include <stdint.h>
#include <dyld-interpose.h>
#include <payload_dylib/crashreporter.h>
#include <paleinfo.h>
#include <spawn.h>
#include <xpc/xpc.h>

#define spin() _spin()

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#define CHECK_ERROR(action, msg) do { \
 ret = action; \
 if (unlikely(ret)) { \
_panic(msg ": %d (%s)\n", errno, strerror(errno)); \
 } \
} while (0)

extern uint64_t pflags;
extern char** environ;

extern bool bound_libiosexec;
void _spin(void);

extern int (*spawn_hook_common_p)(pid_t *restrict pid, const char *restrict path,
					   const posix_spawn_file_actions_t *restrict file_actions,
					   const posix_spawnattr_t *restrict attrp,
					   char *const argv[restrict],
					   char *const envp[restrict],
					   void *pspawn_org);

extern void (*MSHookFunction_p)(void *symbol, void *replace, void **result);
void initSpawnHooks(void);
void InitDaemonHooks(void);
void InitXPCHooks(void);
void load_bootstrapped_jailbreak_env(void);
int bootscreend_draw_image(const char* image_path);
const char* set_tweakloader_path(const char* path);
extern int (*spawn_hook_common_p)(pid_t *restrict pid, const char *restrict path,
					   const posix_spawn_file_actions_t *restrict file_actions,
					   const posix_spawnattr_t *restrict attrp,
					   char *const argv[restrict],
					   char *const envp[restrict],
					   void *pspawn_org);
_Noreturn void _panic(char* fmt, ...);


#endif
