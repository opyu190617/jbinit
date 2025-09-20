#include <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <substrate.h>
#include <CoreServices/FSNode.h>
#include <string.h>
#include <spawn.h>
#include <CoreFoundation/CFURLPriv.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <universalhooks/hooks.h>
#include <libjailbreak/libjailbreak.h>

NSURL* (*orig_LSGetInboxURLForBundleIdentifier)(NSString* bundleIdentifier)=NULL;
NSURL* new_LSGetInboxURLForBundleIdentifier(NSString* bundleIdentifier)
{
    NSURL* pathURL = orig_LSGetInboxURLForBundleIdentifier(bundleIdentifier);

    if( ![bundleIdentifier hasPrefix:@"com.apple."]
            && [pathURL.path hasPrefix:@"/var/mobile/Library/Application Support/Containers/"])
    {
        NSLog(@"redirect Inbox %@: %@", bundleIdentifier, pathURL);
        pathURL = [NSURL fileURLWithPath:[NSString stringWithFormat:@"/var/jb/%@", pathURL.path]];
    }

    return pathURL;
}

void lsdRootlessInit(void) {
    NSLog(@"lsdRootlessInit...");
    MSImageRef coreServicesImage = MSGetImageByName("/System/Library/Frameworks/CoreServices.framework/CoreServices");
    
    void* _LSGetInboxURLForBundleIdentifier = MSFindSymbol(coreServicesImage, "__LSGetInboxURLForBundleIdentifier");
    NSLog(@"coreServicesImage=%p, _LSGetInboxURLForBundleIdentifier=%p", coreServicesImage, _LSGetInboxURLForBundleIdentifier);
    if(_LSGetInboxURLForBundleIdentifier)
        MSHookFunction(_LSGetInboxURLForBundleIdentifier, (void *)&new_LSGetInboxURLForBundleIdentifier, (void **)&orig_LSGetInboxURLForBundleIdentifier);
}

int platform;
int (*_LSServer_RebuildApplicationDatabases_orig)(void);
int _LSServer_RebuildApplicationDatabases_hook(void) {
    int retval = _LSServer_RebuildApplicationDatabases_orig();
    pid_t pid;
    
    if (platform != PLATFORM_BRIDGEOS) {
        if (rootful) {
            posix_spawn(&pid, "/usr/bin/uicache", NULL, NULL, (char*[]){ "/usr/bin/uicache", "-a", NULL }, NULL);
        } else {
            posix_spawn(&pid, "/var/jb/usr/bin/uicache", NULL, NULL, (char*[]){ "/var/jb/usr/bin/uicache", "-a", NULL }, NULL);
        }
    }
    
    if (platform == PLATFORM_IOS) {
        posix_spawn(&pid, "/cores/binpack/usr/bin/uicache", NULL, NULL, (char*[]){ "/cores/binpack/usr/bin/uicache", "-p", "/cores/binpack/Applications/palera1nLoader.app", NULL }, NULL);
    } else if (platform == PLATFORM_TVOS) {
        posix_spawn(&pid, "/cores/binpack/usr/bin/uicache", NULL, NULL, (char*[]){ "/cores/binpack/usr/bin/uicache", "-p", "/cores/binpack/Applications/palera1nLoaderTV.app", NULL }, NULL);
    }
    return retval;
}

#define COMPAT_BUGGY_ELLEKIT

#ifdef COMPAT_BUGGY_ELLEKIT
static uint32_t* find_insn_maskmatch_match(uint8_t* data, size_t size, uint32_t* matches, uint32_t* masks, int count) {
    int found = 0;
    if(sizeof(matches) != sizeof(masks))
        return NULL;
    
    uint32_t* retval = NULL;
    uint32_t* current_inst = (uint32_t*)data;
    while ((uintptr_t)current_inst < (uintptr_t)data + size - 4 - (count*4)) {
        current_inst++;
        found = 1;
        for(int i = 0; i < count; i++) {
            if((matches[i] & masks[i]) != (current_inst[i] & masks[i])) {
                found = 0;
                break;
            }
        }
        if (found) {
            if (!retval)
                retval = current_inst;
            else {
                NSLog(@"found twice!");
                return NULL;
            }
        }
    }
    
    return retval;
}

static uint32_t* find_prev_insn(uint32_t* from, uint32_t num, uint32_t insn, uint32_t mask) {
    while(num) {
        if((*from & mask) == (insn & mask)) {
            return from;
        }
        from--;
        num--;
    }
    return NULL;
}

static void* find__LSServer_RebuildApplicationDatabases(void) {
    char coreservices_path[PATH_MAX] = "/System/Library/Frameworks/CoreServices.framework/CoreServices";
    int coreservices_image_index = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if(!strcmp(_dyld_get_image_name(i), coreservices_path)) {
            coreservices_image_index = i;
            break;
        }
    }
    intptr_t slide = _dyld_get_image_vmaddr_slide(coreservices_image_index);
    struct mach_header_64 *mach_header = (struct mach_header_64*)_dyld_get_image_header(coreservices_image_index);

    uintptr_t cmd_current = (uintptr_t)(mach_header + 1);
    uintptr_t cmd_end = cmd_current + mach_header->sizeofcmds;

    uint32_t* text_start = NULL;
    size_t text_size = 0;
    for (uint32_t i = 0; i < mach_header->ncmds && cmd_current <= cmd_end; i++) {
        const struct segment_command_64 *cmd;

        cmd = (struct segment_command_64*)cmd_current;
        cmd_current += cmd->cmdsize;

        if (cmd->cmd != LC_SEGMENT_64 || strcmp(cmd->segname, "__TEXT")) {
            continue;
        }

        text_start = (uint32_t*)(cmd->vmaddr + slide);
        text_size = (size_t)cmd->vmsize;
    }

    if (!text_size) {
        NSLog(@"failed to find CoreServices __TEXT segment");
        return NULL;
    }

    uint32_t matches[] = {
        0xaa0003f0, // mov x{16-31}, x0
        0x90000003, // adrp x3,cf_SendingappStateChangedNotificationwithpayload@PAGE
        0x91000063, // add x3, cf_SendingappStateChangedNotificationwithpayload@PAGEOFF
        0x52800120, // mov w0, #0x9
        0x52800001, // mov w1, #0x0
        0xd2800002, // mov x2, #0x0
        0x94000000  // bl __LSLogStepStart
    };

    uint32_t masks[] = {
        0xfffffff0,
        0x9f00001f,
        0xffc003ff,
        0xffffffff,
        0xffffffff,
        0xffffffff,
        0xfc000000
    };

    uint32_t* __LSServer_RebuildApplicationDatabases = NULL;
    uint32_t* __LSServer_RebuildApplicationDatabases_mid = find_insn_maskmatch_match((uint8_t*)text_start, text_size, matches, masks, sizeof(matches)/sizeof(uint32_t));
    NSLog(@"__LSServer_RebuildApplicationDatabases_mid=%p", __LSServer_RebuildApplicationDatabases_mid);
    
    if (__LSServer_RebuildApplicationDatabases_mid) {
        __LSServer_RebuildApplicationDatabases = find_prev_insn(__LSServer_RebuildApplicationDatabases_mid, 25, 0xd10003ff, 0xffc003ff); // sub sp, sp, *
        if (__LSServer_RebuildApplicationDatabases[-1] == 0xd503237f) __LSServer_RebuildApplicationDatabases -= 1; // pacibsp

        NSLog(@"__LSServer_RebuildApplicationDatabases=%p", __LSServer_RebuildApplicationDatabases);
    }
    
    return __LSServer_RebuildApplicationDatabases;
}
#endif

void lsdUniversalInit(void) {
    NSLog(@"lsdUniversalInit...");
    platform = jailbreak_get_platform();
    MSImageRef coreServicesImage = MSGetImageByName("/System/Library/Frameworks/CoreServices.framework/CoreServices");
    void* __LSServer_RebuildApplicationDatabases = MSFindSymbol(coreServicesImage, "__LSServer_RebuildApplicationDatabases");
    
#ifdef COMPAT_BUGGY_ELLEKIT
    // Older ellekit tvOS has a bug where private symbols can't be found
    // TODO: Remove
    if (!__LSServer_RebuildApplicationDatabases) {
        NSLog(@"Buggy ellekit detected!");
        __LSServer_RebuildApplicationDatabases = find__LSServer_RebuildApplicationDatabases();
    }
#endif
    
    if (__LSServer_RebuildApplicationDatabases) {
        MSHookFunction(__LSServer_RebuildApplicationDatabases, (void*)&_LSServer_RebuildApplicationDatabases_hook, (void**)&_LSServer_RebuildApplicationDatabases_orig);
    } else {
        NSLog(@"Could not find __LSServer_RebuildApplicationDatabases");
    }
    
}
