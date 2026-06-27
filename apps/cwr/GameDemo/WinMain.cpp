#ifdef _WIN32

#include <windows.h>
#include <cstdio>
#include <cstring>
#include "GameDemoApplication.hpp"
#include <Poseidon/Core/ProgressSystem.hpp>
#include <Poseidon/Foundation/Common/ConsoleUtils.hpp>
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    if (!strstr(szCmdLine, "--check"))
        Poseidon::Foundation::attachParentConsole();
    GameDemoApplication app;
    return app.Run(hInst, szCmdLine, sw);
}

#else // Linux

#include "GameDemoApplication.hpp"
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL.h>
#include <jni.h>
#include <cstdlib>
#include <unistd.h>
#include <string>

extern "C" {
    static bool g_folderPickerDone = false;
    static std::string g_pickedFolder = "";

    JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeFolderPicked(JNIEnv* env, jclass cls, jstring path) {
        if (path) {
            const char* str = env->GetStringUTFChars(path, nullptr);
            g_pickedFolder = str;
            env->ReleaseStringUTFChars(path, str);
        }
        g_folderPickerDone = true;
    }
}

static std::string showFolderDialog() {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return "";

    jclass activityClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (!activityClass) return "";

    jmethodID openPicker = env->GetStaticMethodID(activityClass, "openFolderPicker", "()V");
    if (!openPicker) return "";
    
    g_folderPickerDone = false;
    g_pickedFolder = "";
    
    env->CallStaticVoidMethod(activityClass, openPicker);
    
    while (!g_folderPickerDone) {
        SDL_Delay(50);
    }
    
    return g_pickedFolder;
}
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
#ifdef __ANDROID__
    std::string assetPath = showFolderDialog();
    bool chdirSuccess = false;
    
    if (!assetPath.empty()) {
        setenv("TMPDIR", assetPath.c_str(), 1);
        if (chdir(assetPath.c_str()) == 0) {
            chdirSuccess = true;
            SDL_Log("Successfully set working directory to: %s", assetPath.c_str());
        } else {
            SDL_Log("Failed to chdir to %s. Did you grant 'All Files Access' permission in Android Settings?", assetPath.c_str());
        }
    }
    
    if (!chdirSuccess) {
        char* prefPath = SDL_GetPrefPath("cwr", "Poseidon");
        if (prefPath) {
            std::string fullPath = prefPath;
            if (!fullPath.empty() && fullPath.back() == '/') fullPath.pop_back();
            fullPath += "/Poseidon";
            setenv("TMPDIR", fullPath.c_str(), 1);
            chdir(fullPath.c_str());
            SDL_free(prefPath);
            SDL_Log("Fell back to default private directory: %s", fullPath.c_str());
        }
    }
#endif
    GameDemoApplication app;
    return app.Run(argc, argv);
}

#endif
