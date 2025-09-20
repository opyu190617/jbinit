#define __APPLE_API_PRIVATE

#include <stdio.h>
#include <paleinfo.h>
#include <xpc/xpc.h>
#include <stdint.h>
#include <spawn.h>
#include <errno.h>
#include <sys/utsname.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <libjailbreak/libjailbreak.h>
#include <payload_dylib/common.h>
#include <payload_dylib/crashreporter.h>
#include <sandbox/private.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#define HOOK_DYLIB_PATH "/cores/binpack/usr/lib/systemhook.dylib"
#define HOOKING_FW_PATH "/cores/binpack/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate"

bool has_verbose_boot;
uint64_t pflags;
void (*MSHookFunction_p)(void *symbol, void *replace, void **result) = NULL;
int (*spawn_hook_common_p)(pid_t *restrict pid, const char *restrict path,
					   const posix_spawn_file_actions_t *restrict file_actions,
					   const posix_spawnattr_t *restrict attrp,
					   char *const argv[restrict],
					   char *const envp[restrict],
					   void *pspawn_org) = NULL;

void _spin(void) {
  fprintf(stderr, "An error occured");
  while (1) {
    sleep(5);
  }
}

char* generate_sandbox_extensions(void) {
    static char* extensions = NULL;
    if (extensions) return extensions;
    asprintf(&extensions, "%s|%s",
             sandbox_extension_issue_mach("com.apple.security.exception.mach-lookup.global-name", "in.palera.palera1nd.systemwide", 0),
             sandbox_extension_issue_mach("com.apple.app-sandbox.mach", "in.palera.palera1nd.systemwide", 0)
             );
    return extensions;
}

void load_bootstrapped_jailbreak_env(void)
{
  struct utsname name;
  int ret = uname(&name);
  if (!ret){
    if (atoi(name.release) < 20) {
      return;
    } else {
      char jbPath[150];
      ret = jailbreak_get_prebootPath(jbPath);
        void *systemhook_handle = dlopen(HOOK_DYLIB_PATH, RTLD_NOW);
        if (systemhook_handle) {
          char **pJB_RootPath = dlsym(systemhook_handle, "JB_RootPath");
          if (!ret) {
            if (pJB_RootPath) {
              setenv("JB_ROOT_PATH", jbPath, 1);
              char *old_rootPath = *pJB_RootPath;
              *pJB_RootPath = strdup(jbPath);
              free(old_rootPath);
            }
#ifdef HAVE_SYSTEMWIDE_IOSEXEC
            if (!bound_libiosexec) {
              init_libiosexec_hook_with_ellekit = dlsym(systemhook_handle, "init_libiosexec_hook_with_ellekit");
              if (init_libiosexec_hook_with_ellekit) {
              if (!init_libiosexec_hook_with_ellekit())
                bound_libiosexec = true;
              }
            }
#endif
          } else if (ret == ENOENT) {
              setenv("JB_ROOT_PATH", "/var/jb", 1);
              char *old_rootPath = *pJB_RootPath;
              *pJB_RootPath = strdup("/var/jb");
              free(old_rootPath);
          }
          dlclose(systemhook_handle);
        }
    }
  }
}

const char* set_tweakloader_path(const char* path) {
  const char* retval = "failed to set tweakloader path";
  void *systemhook_handle = dlopen(HOOK_DYLIB_PATH, RTLD_NOW);
  if (systemhook_handle) {
    char **pJB_TweakLoaderPath = dlsym(systemhook_handle, "JB_TweakLoaderPath");
    if (pJB_TweakLoaderPath) {
      setenv("JB_TWEAKLOADER_PATH", path, 1);
      char* old_tweakLoaderPath = *pJB_TweakLoaderPath;
      *pJB_TweakLoaderPath = strdup(path);
      free(old_tweakLoaderPath);
    } else retval = dlerror();
    dlclose(systemhook_handle);
  } else {
    retval = dlerror();
  }
  return retval;
}

uint64_t load_pflags(void) {
  pflags = strtoull(getenv("JB_PINFO_FLAGS"), NULL, 0);
  return pflags;
}

void _panic_on_signal(int signal) {
    char panicBuffer[1024];
    panicBuffer[1023] = '\0';
    FILE* f = fopen("/cores/panic.txt", "r");
    if (!f) {
        _panic("payload_dylib: panic signal received but could not open panic file");
    }
    fread(panicBuffer, 1023, 1, f);
    fclose(f);
    _panic("%s", panicBuffer);
}

__attribute__((constructor))void launchd_hook_main(void) {
  if (getpid() != 1) return;
    signal(SIGUSR1, _panic_on_signal);
    has_verbose_boot = (strcmp(getenv("JB_HAS_VERBOSE_BOOT"), "1") == 0);
  
  int fd_console = open("/dev/console",O_RDWR|O_SYNC|O_CLOEXEC,0);
  if (fd_console == -1) {
    _panic("payload.dylib cannot open /dev/console: %d (%s)", errno, strerror(errno));
  }
    
  
  dup2(fd_console, STDIN_FILENO);
  dup2(fd_console, STDOUT_FILENO);
  dup2(fd_console, STDERR_FILENO);

  printf("=========== hello from payload.dylib ===========\n");
  crashreporter_start();
  setenv("JB_SANDBOX_EXTENSIONS", generate_sandbox_extensions(), 1);
  load_pflags();
  pid_t pid;
  int ret, status;
  CHECK_ERROR(posix_spawn(&pid, "/cores/payload", NULL, NULL, (char*[]){"/cores/payload","-f",NULL},environ), "could not spawn payload");
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) != 0) {
      _panic("/cores/payload exited with status code %d\n", WEXITSTATUS(status));
    }
  } else if (WIFSIGNALED(status)) {
      _panic("/cores/payload exited abnormally: signal %d\n", WTERMSIG(status));
  } else {
      _panic("/cores/payload exited abnormally");
  }

  if ((pflags & palerain_option_setup_rootful)) {
    int32_t initproc_spawned = 1;
    CHECK_ERROR(sysctlbyname("kern.initproc_spawned", NULL, NULL, &initproc_spawned, 4), "sysctl kern.initproc_spawned=1");
    CHECK_ERROR(unmount("/cores/binpack/Applications", MNT_FORCE), "unmount(/cores/binpack/Applications)");
    CHECK_ERROR(unmount("/cores/binpack", MNT_FORCE), "unmount(/cores/binpack)");
    printf("Rebooting\n");
    kern_return_t failed = host_reboot(mach_host_self(), 0x1000);
    _panic("reboot failed: %d (%s)\n", failed, mach_error_string(failed));
  }

  if (
      ((pflags & palerain_option_verbose_boot) == 0)
      && (jailbreak_get_platform() != PLATFORM_TVOS || getenv("XPC_USERSPACE_REBOOTED"))
      ) {
          posix_spawn(&pid, "/cores/payload", NULL, NULL, (char*[]){"bootscreend" ,NULL}, (char*[]){ "XPC_NULL_BOOTSTRAP=1" });
      }

  void* systemhook_handle = dlopen(HOOK_DYLIB_PATH, RTLD_NOW);
  if (!systemhook_handle) {
    _panic("dlopen systemhook failed: %s\n", dlerror());
  }
  spawn_hook_common_p = dlsym(systemhook_handle, "spawn_hook_common");
  if (!spawn_hook_common_p) {
    _panic("symbol spawn_hook_common not found in " HOOK_DYLIB_PATH ": %s\n", dlerror());
  }

  void* ellekit_handle = dlopen(HOOKING_FW_PATH, RTLD_NOW);
  if (!ellekit_handle) {
    _panic("dlopen hooking framework failed: %s\n", dlerror());
  }

  MSHookFunction_p = dlsym(ellekit_handle, "MSHookFunction");
  if (!MSHookFunction_p) {
    _panic("symbol MSHookFunction not found in " HOOKING_FW_PATH ": %s\n", dlerror());
  }

  initSpawnHooks();
  InitDaemonHooks();
  InitXPCHooks();
    
    signal(SIGUSR1, SIG_DFL);
  printf("=========== bye from payload.dylib ===========\n");
}

_Noreturn
void
abort_with_payload(uint32_t reason_namespace, uint64_t reason_code, void *payload,
            uint32_t payload_size, const char *reason_string,
                   uint64_t reason_flags);

_Noreturn void _panic(char* fmt, ...) {
  char reason[1024], reason_real[1024];
  va_list va;
  va_start(va, fmt);
  vsnprintf(reason, 1024, fmt, va);
  va_end(va);
  snprintf(reason_real, 1024, "payload_dylib: %s", reason);
  if (has_verbose_boot) {
    printf("%s\n", reason_real);
    printf("jbinit DIED!\n");
    sleep(60);
  }
  abort_with_payload(42, 0x69, NULL, 0, reason_real, 0);
}
