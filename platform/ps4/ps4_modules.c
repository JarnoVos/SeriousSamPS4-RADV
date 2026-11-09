/* System sprx files stay unpatched until sceSysmoduleLoadModuleInternal runs.
 * Calling them earlier is PRX_NOT_RESOLVED_FUNCTION. Same IDs as the probe. */
#include <stdint.h>
#include <stdio.h>

int32_t sceSysmoduleLoadModuleInternal(uint32_t id);

void ss_ps4_load_modules(void) {
    static int done;
    static const struct {
        uint32_t id;
        const char *name;
    } modules[] = {
        {0x80000010, "SystemService"},
        {0x80000011, "UserService"},
        {0x80000022, "VideoOut"},
        {0x80000024, "Pad"},
        {0x80000001, "AudioOut"},
    };
    unsigned i;
    if (done)
        return;
    done = 1;
    for (i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        int32_t ret = sceSysmoduleLoadModuleInternal(modules[i].id);
        fprintf(stderr, "PS4 module %s = %#x\n", modules[i].name, (unsigned)ret);
    }
}
