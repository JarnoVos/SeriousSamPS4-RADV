// Statically linked equivalents of the engine's game, entity and shader DLLs.
#include <Engine/Engine.h>
#include <Engine/Base/DynamicLoader.h>
#include <string.h>

extern void *ss_ps4_find_export(const char *name);

class CStaticLoader : public CDynamicLoader {
    const char *error;
public:
    explicit CStaticLoader(const char *name) : error(nullptr) {
        if (name && !strstr(name, "Entities") && !strstr(name, "Game") &&
            !strstr(name, "Shaders") && !strstr(name, "vorbisfile") &&
            !strstr(name, "amp11lib")) error = "Module is not linked into the PS4 client";
    }
    void *FindSymbol(const char *name) override {
        if (error) return nullptr;
        void *symbol = ss_ps4_find_export(name);
        if (!symbol) error = "Export is absent from the PS4 static module table";
        return symbol;
    }
    const char *GetError() override { return error; }
};

CDynamicLoader *CDynamicLoader::GetInstance(const char *name) {
    return new CStaticLoader(name);
}
CTFileName CDynamicLoader::ConvertLibNameToPlatform(const char *name) {
    return CTFileName(CTString(name));
}
