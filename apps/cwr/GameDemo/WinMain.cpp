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
#include <cstdlib>
#include <unistd.h>
#include <string>
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
#ifdef __ANDROID__
    char* prefPath = SDL_GetPrefPath("cwr", "Poseidon");
    if (prefPath) {
        std::string fullPath = prefPath;
        if (!fullPath.empty() && fullPath.back() == '/') fullPath.pop_back();
        fullPath += "/Poseidon";
        setenv("TMPDIR", fullPath.c_str(), 1);
        chdir(fullPath.c_str());
        SDL_free(prefPath);
    }
#endif
    GameDemoApplication app;
    return app.Run(argc, argv);
}

#endif
