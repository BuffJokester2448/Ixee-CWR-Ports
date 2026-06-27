#ifdef _WIN32

#include <windows.h>
#include <cstring>
#include "GameApplication.hpp"
#include <Poseidon/Core/ProgressSystem.hpp> // Needed for complete type in Application
#include <Poseidon/Foundation/Common/ConsoleUtils.hpp>
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    if (!strstr(szCmdLine, "--check"))
        Poseidon::Foundation::attachParentConsole();
    GameApplication app;
    return app.Run(hInst, szCmdLine, sw);
}

#else // Linux

#include "GameApplication.hpp"
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_filesystem.h>
#include <cstdlib>
#include <unistd.h>
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
#ifdef __ANDROID__
    char* prefPath = SDL_GetPrefPath("cwr", "Poseidon");
    if (prefPath) {
        setenv("TMPDIR", prefPath, 1);
        chdir(prefPath);
        SDL_free(prefPath);
    }
#endif
    GameApplication app;
    return app.Run(argc, argv);
}

#endif
