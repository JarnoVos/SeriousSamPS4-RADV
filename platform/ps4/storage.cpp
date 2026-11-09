#include "storage.h"
#include <orbis/libkernel.h>
#include "libjbc.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace ps4 {
namespace {
unsigned ownedMounts;
bool ownedDataMount;
bool ownedUserMount;

#ifdef SS_PS4_TSE
const char* userFolder = "serioussamse";
const char* dataFolder = "serioussamse-game";
const char* requiredFiles[] = {"SE1_00.gro", "SE1_00_Extra.gro", "SE1_00_Levels.gro", "SE1_00_Music.gro", "SE1_10b.gro", "Scripts/Game_startup.ini", "Data/NoCRC.lst"};
#else
const char* userFolder = "serioussam";
const char* dataFolder = "serioussam-game";
const char* requiredFiles[] = {"1_00.gro", "1_00_music.gro", "SE1_10b.gro", "Levels/01_Hatshepsut.wld", "Scripts/Game_startup.ini", "Data/NoCRC.lst"};
#endif

bool gameReadable(const char* folder) {
    for (const char* name : requiredFiles) {
        char file[600];
        const int length = std::snprintf(file, sizeof(file), "%s/%s", folder, name);
        if (length < 0 || size_t(length) >= sizeof(file)) return false;
        int fd = sceKernelOpen(file, O_RDONLY, 0);
        if (fd < 0) return false;
        char header[2]{};
        auto bytes = sceKernelRead(fd, header, sizeof(header));
        sceKernelClose(fd);
        if (bytes != 2) return false;
        if (std::strstr(name, ".gro") && (header[0] != 'P' || header[1] != 'K')) return false;
    }
    return true;
}

bool restore(const jbc_cred& original) {
    // Do not continue to game startup when restoration fails.
    for (int attempt = 0; attempt < 3; ++attempt)
        if (!jbc_set_cred(&original)) return true;
    sceKernelDebugOutText(0, "Serious Sam storage: credential restoration failed\n");
    return false;
}

StorageResult result(StorageStatus status, const char* path = "") {
    StorageResult out;
    out.status = status;
    std::snprintf(out.path, sizeof(out.path), "%s", path);
    char message[600];
    std::snprintf(message, sizeof(message), "Serious Sam storage: status=%d path=%s\n", int(status), out.path);
    sceKernelDebugOutText(0, message);
    return out;
}
}

StorageResult resolveGameData(const char* preferred, bool allowSandboxMapping) {
    if (!preferred || preferred[0] != '/' || std::strlen(preferred) >= 480)
        return result(StorageStatus::InvalidPath);
    if (gameReadable(preferred)) return result(StorageStatus::Ready, preferred);
    // An explicit internal/custom path must not silently select unrelated USB data.
    if (std::strncmp(preferred, "/mnt/usb", 8) || preferred[8] < '0' || preferred[8] > '7' || preferred[9] != '/')
        return result(StorageStatus::NotFound);
    const char* suffix = preferred + 10;
    char candidates[8][512];
    char aliases[8][512];
    for (int slot = 0; slot < 8; ++slot) {
        std::snprintf(candidates[slot], sizeof(candidates[slot]), "/mnt/usb%d/%s", slot, suffix);
        std::snprintf(aliases[slot], sizeof(aliases[slot]), "/ssam_usb%d/%s", slot, suffix);
        if (gameReadable(candidates[slot])) return result(StorageStatus::Ready, candidates[slot]);
        if (gameReadable(aliases[slot])) return result(StorageStatus::Ready, aliases[slot]);
    }
    if (!allowSandboxMapping) return result(StorageStatus::NotFound);

    jbc_cred original{};
    if (jbc_get_cred(&original)) return result(StorageStatus::ServiceUnavailable);
    jbc_cred external = original;
    if (jbc_jailbreak_cred(&external) || jbc_set_cred(&external)) {
        return result(restore(original) ? StorageStatus::AccessFailed : StorageStatus::RestoreFailed);
    }
    unsigned present = 0;
    for (int slot = 0; slot < 8; ++slot)
        if (gameReadable(candidates[slot])) present |= 1u << slot;
    if (!restore(original)) return result(StorageStatus::RestoreFailed);
    if (!present) return result(StorageStatus::NotFound);

    for (int slot = 0; slot < 8; ++slot) {
        if (!(present & (1u << slot))) continue;
        char source[32], alias[32];
        std::snprintf(source, sizeof(source), "/mnt/usb%d", slot);
        std::snprintf(alias, sizeof(alias), "ssam_usb%d", slot);
        const int error = jbc_mount_in_sandbox(source, alias);
        if (!error) ownedMounts |= 1u << slot;
        // The helper temporarily changes credentials too, including on errors.
        if (!restore(original)) return result(StorageStatus::RestoreFailed);
        if (gameReadable(aliases[slot])) return result(StorageStatus::Ready, aliases[slot]);
    }
    return result(StorageStatus::MountFailed);
}

// /data is readable from this process, but a real PS4 sandbox rejects chdir
// outside the jail. Mount the game directory at /ssam_data and enter that.
StorageStatus mapHostData(const char* systemPath, char* sandboxPath, size_t sandboxPathSize) {
    if (!systemPath || std::strncmp(systemPath, "/data/", 6) != 0)
        return StorageStatus::InvalidPath;
    jbc_cred original{};
    if (jbc_get_cred(&original)) return StorageStatus::ServiceUnavailable;
    const int error = jbc_mount_in_sandbox(systemPath, "ssam_data");
    if (!error) ownedDataMount = true;
    // The helper changes credentials even when the mount fails.
    if (!restore(original)) return StorageStatus::RestoreFailed;
    if (error) return StorageStatus::MountFailed;
    const int length = std::snprintf(sandboxPath, sandboxPathSize, "/ssam_data");
    if (length < 0 || size_t(length) >= sandboxPathSize) return StorageStatus::InvalidPath;
    if (!gameReadable(sandboxPath)) return StorageStatus::NotFound;
    return StorageStatus::Ready;
}

void releaseGameDataMounts() {
    if (!ownedMounts && !ownedDataMount && !ownedUserMount) return;
    jbc_cred original{};
    if (jbc_get_cred(&original)) return;
    if (ownedDataMount) {
        const int error = jbc_unmount_in_sandbox("ssam_data");
        if (!restore(original)) return;
        if (!error) ownedDataMount = false;
    }
    if (ownedUserMount) {
        const int error = jbc_unmount_in_sandbox("ssam_user");
        if (!restore(original)) return;
        if (!error) ownedUserMount = false;
    }
    for (int slot = 0; slot < 8; ++slot) {
        if (!(ownedMounts & (1u << slot))) continue;
        char name[32];
        std::snprintf(name, sizeof(name), "ssam_usb%d", slot);
        const int error = jbc_unmount_in_sandbox(name);
        if (!restore(original)) return;
        if (!error) ownedMounts &= ~(1u << slot);
    }
}

#ifdef SS_PS4_TSE
static char selectedUserRoot[64] = "/data/serioussamse/";
#else
static char selectedUserRoot[64] = "/data/serioussam/";
#endif

struct UserDirPrep {
    const char* host;
    int ans;
};

// Runs with root credentials and the sandbox directory left as it was.
// A full jailbreak before the mount points getcwd at the real root, so the
// mount lands outside the sandbox and /ssam_user does not exist afterwards.
void prepareUserTree(void* argument) {
    UserDirPrep* prep = static_cast<UserDirPrep*>(argument);
    const char* subs[] = {"", "/SaveGame", "/Controls", "/Scripts", "/Data", "/Temp",
                          "/Demos", "/Players", "/ScreenShots", "/Levels"};
    prep->ans = 0;
    for (const char* sub : subs) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s%s", prep->host, sub);
        if (mkdir(path, 0777) != 0 && errno != EEXIST)
            prep->ans = errno;
        if (chmod(path, 0777) != 0 && !prep->ans)
            prep->ans = errno;
    }
}

bool canWrite(const char* dir) {
    char probe[512];
    std::snprintf(probe, sizeof(probe), "%sSeriousSam.write", dir);
    FILE* file = std::fopen(probe, "w");
    if (!file) return false;
    std::fputs("ok\n", file);
    std::fclose(file);
    std::remove(probe);
    return true;
}

StorageStatus prepareUserDirectoryBody() {
    char host[256];
    std::snprintf(host, sizeof(host), "/data/%s", userFolder);
    std::snprintf(selectedUserRoot, sizeof(selectedUserRoot), "/data/%s/", userFolder);
    UserDirPrep prep{host, -1};
    jbc_run_as_root(prepareUserTree, &prep, CWD_KEEP);
    if (prep.ans)
        std::fprintf(stderr, "PS4 user tree %s status %d\n", host, prep.ans);
    if (canWrite(selectedUserRoot)) {
        std::fprintf(stderr, "PS4 user dir: %s\n", selectedUserRoot);
        return StorageStatus::Ready;
    }
    const int error = jbc_mount_in_sandbox(host, "ssam_user");
    if (!error) ownedUserMount = true;
    if (!error && canWrite("/ssam_user/")) {
        std::snprintf(selectedUserRoot, sizeof(selectedUserRoot), "/ssam_user/");
        std::fprintf(stderr, "PS4 user dir: %s -> %s\n", host, selectedUserRoot);
        return StorageStatus::Ready;
    }
    std::fprintf(stderr, "PS4 user dir not writable at %s or /ssam_user (%d, mount %#x)\n",
                 selectedUserRoot, errno, error);
    return StorageStatus::MountFailed;
}

const char* userRootStorage() { return selectedUserRoot; }
}

static char selectedDataRoot[512];
const char* ss_ps4_data_root() { return selectedDataRoot; }
const char* ss_ps4_user_root() { return ps4::userRootStorage(); }

// Called before engine initialization; keep the mounts alive for all archive reads.
bool ss_ps4_init_data() {
    char preferred[512];
    std::snprintf(preferred, sizeof(preferred), "/data/%s", ps4::dataFolder);
    ps4::StorageResult selected = ps4::resolveGameData(preferred, false);
    if (selected.status != ps4::StorageStatus::Ready) {
        std::snprintf(preferred, sizeof(preferred), "/mnt/usb0/%s", ps4::dataFolder);
        selected = ps4::resolveGameData(preferred, true);
    }
    if (selected.status != ps4::StorageStatus::Ready) {
        std::fprintf(stderr, "Missing/unreadable external game data: /data/%s or USB root/%s (status=%d)\n",
                     ps4::dataFolder, ps4::dataFolder, int(selected.status));
        // Never run further helpers with credentials whose restoration failed.
        if (selected.status != ps4::StorageStatus::RestoreFailed) ps4::releaseGameDataMounts();
        return false;
    }
    char active[512];
    std::snprintf(active, sizeof(active), "%s", selected.path);
    if (std::strncmp(selected.path, "/data/", 6) == 0) {
        char sandbox[32];
        const ps4::StorageStatus mapped = ps4::mapHostData(selected.path, sandbox, sizeof(sandbox));
        if (mapped == ps4::StorageStatus::Ready)
            std::snprintf(active, sizeof(active), "%s", sandbox);
        else if (mapped != ps4::StorageStatus::ServiceUnavailable)
            std::fprintf(stderr, "Using %s without a sandbox mount (status=%d)\n",
                         selected.path, int(mapped));
    }
    // The engine turns "<root>/Bin/" into the absolute data directory. A real
    // PS4 rejects chdir outside /app0, and leaving main calls a forbidden _exit.
    std::snprintf(selectedDataRoot, sizeof(selectedDataRoot), "%s", active);
    const ps4::StorageStatus userDir = ps4::prepareUserDirectoryBody();
    if (userDir == ps4::StorageStatus::RestoreFailed) return false;
    if (chdir("/app0"))
        std::fprintf(stderr, "Continuing without /app0 as the working directory\n");
    std::atexit(ps4::releaseGameDataMounts);
    std::fprintf(stderr, "Selected external game data: %s\n", active);
    return true;
}
