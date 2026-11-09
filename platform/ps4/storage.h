#pragma once

namespace ps4 {
enum class StorageStatus { Ready = 0, NotFound = 1, ServiceUnavailable = 2,
    AccessFailed = 3, RestoreFailed = 4, MountFailed = 5, InvalidPath = 6 };
struct StorageResult {
    StorageStatus status = StorageStatus::NotFound;
    char path[512]{};
};
StorageResult resolveGameData(const char* preferred, bool allowSandboxMapping);
void releaseGameDataMounts();
}
