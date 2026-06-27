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
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <cstdlib>
#include <unistd.h>
#include <string>

static bool s_dialogDone = false;
static std::string s_dialogPath;
static void SDLCALL onFolderSelected(void* userdata, const char* const* filelist, int filter)
{
    if (filelist && filelist[0])
        s_dialogPath = filelist[0];
    s_dialogDone = true;
}

static std::string showFolderDialog()
{
    s_dialogDone = false;
    s_dialogPath.clear();
    
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return "";
    }
    
    SDL_ShowOpenFolderDialog(onFolderSelected, nullptr, nullptr, nullptr, false);
    
    while (!s_dialogDone)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                s_dialogDone = true;
        }
        SDL_Delay(16);
    }
    
    return s_dialogPath;
}
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
#ifdef __ANDROID__
    std::string assetPath = showFolderDialog();
    if (!assetPath.empty()) {
        setenv("TMPDIR", assetPath.c_str(), 1);
        chdir(assetPath.c_str());
    } else {
        char* prefPath = SDL_GetPrefPath("cwr", "Poseidon");
        if (prefPath) {
            std::string fullPath = prefPath;
            if (!fullPath.empty() && fullPath.back() == '/') fullPath.pop_back();
            fullPath += "/Poseidon";
            setenv("TMPDIR", fullPath.c_str(), 1);
            chdir(fullPath.c_str());
            SDL_free(prefPath);
        }
    }
#endif
    GameDemoApplication app;
    return app.Run(argc, argv);
}

#endif
