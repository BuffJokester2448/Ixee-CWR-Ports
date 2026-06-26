#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <cstdlib>
#include <sys/stat.h>
#include <string>

namespace {

void ensureDirectory(const std::string& path) {
    if (path.empty()) return;
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string partial = path.substr(0, i);
            mkdir(partial.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
}

std::string getXdgDir(const char* envVar, const char* defaultSuffix, const char* appName) {
    std::string base;
    const char* envVal = getenv(envVar);
    if (envVal && envVal[0] != '\0') {
        base = envVal;
    } else {
        const char* home = getenv("HOME");
        if (home && home[0] != '\0') {
            base = std::string(home) + "/" + defaultSuffix;
        } else {
            base = std::string("/tmp");
        }
    }
    std::string dir = base + "/" + appName;
    ensureDirectory(dir);
    return dir;
}

} // anonymous namespace

#ifdef __ANDROID__
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#endif

namespace Poseidon::Foundation {

#ifdef __ANDROID__
static std::string getAndroidPrefPath(const char* appName) {
    std::string result;
    char* prefPath = SDL_GetPrefPath("cwr", appName);
    if (prefPath) {
        result = prefPath;
        SDL_free(prefPath);
        // remove trailing slash if present for consistency
        if (!result.empty() && result.back() == '/') {
            result.pop_back();
        }
        // SDL3 on Android ignores org and app, so we must append it manually
        if (appName && appName[0] != '\0') {
            result += "/";
            result += appName;
        }
    } else {
        result = std::string("/data/local/tmp/") + appName;
    }
    ensureDirectory(result);
    return result;
}
#endif

std::string getUserConfigDir(const char* appName) {
#ifdef __ANDROID__
    return getAndroidPrefPath(appName);
#else
    return getXdgDir("XDG_CONFIG_HOME", ".config", appName);
#endif
}

std::string getUserDataDir(const char* appName) {
#ifdef __ANDROID__
    return getAndroidPrefPath(appName);
#else
    return getXdgDir("XDG_DATA_HOME", ".local/share", appName);
#endif
}

std::string getUserCacheDir(const char* appName) {
#ifdef __ANDROID__
    return getAndroidPrefPath(appName);
#else
    return getXdgDir("XDG_CACHE_HOME", ".cache", appName);
#endif
}

std::string getUserDocumentsDir(const char* appName) {
#ifdef __ANDROID__
    return getAndroidPrefPath(appName);
#else
    // Linux has no per-game "Documents" convention; the XDG data dir is the
    // correct, non-roaming home for user content (mods, editor missions).
    return getXdgDir("XDG_DATA_HOME", ".local/share", appName);
#endif
}

} // namespace Poseidon::Foundation

