#include <PoseidonGLES32/EngineGLES32.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>

#include <SDL3/SDL.h>
#include <PoseidonGLES32/GLESCompat.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>

using namespace Poseidon::Dev;

#include <mutex>
#include <unordered_map>


namespace
{
// process-wide counter of high-severity gl errors.
// the frame layer reads this through Engine::GetDebugErrorCount() to enforce
// I-20, which forbids GL_INVALID_* during a frame.
// the callback runs on the gl thread only, so the counter does not need
// atomic synchronization.
unsigned int s_glHighSeverityErrorCount = 0;

// most recent high-severity gl_debug message.
// surfaced through EngineGLES32::GetLastDebugMessage() so I-20 reports carry
// the actual error text, not just the fact that one occurred.
// this shares the same single-thread assumption as the counter above.
std::string s_glLastHighSeverityMessage;
} // namespace

unsigned int EngineGLES32::GetDebugErrorCount() const
{
    return s_glHighSeverityErrorCount;
}

std::string EngineGLES32::GetLastDebugMessage() const
{
    return s_glLastHighSeverityMessage;
}

namespace
{

// per-error gl debug callback (KHR_debug / GL 4.3+).
// the callback is enabled in all builds because RelWithDebInfo also defines
// NDEBUG, and that is the primary development configuration.
// synchronous delivery is acceptable here because the error text is more
// useful than the cost of reporting it.
// severity maps to log level so the runtime channel filter controls console
// output.
//
// non-high messages are deduplicated by id: the first sighting logs once at
// full severity and later repeats only bump a counter.
// many drivers emit the same medium or low notification repeatedly; without
// deduplication that noise would bury the useful streams.
// high-severity errors are always logged in full because I-20 counts them and
// the violation report needs the message text.
static std::mutex& GetGLDedupMtx() {
    static std::mutex* m = new std::mutex();
    return *m;
}

static std::unordered_map<GLuint, std::uint64_t>& GetGLDedupCounts() {
    static auto* c = new std::unordered_map<GLuint, std::uint64_t>();
    return *c;
}

void GLAPIENTRY GlDebugCallback(GLenum /*source*/, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                const GLchar* message, const void* /*userParam*/)
{
    // suppress nvidia buffer-detail messages (id 131185 = "Buffer object
    // <X> will use VIDEO memory as the source for buffer object operations").
    // they are verbose but not actionable.
    if (id == 131185)
    {
        return;
    }

    const char* sev = severity == GL_DEBUG_SEVERITY_HIGH     ? "HIGH"
                      : severity == GL_DEBUG_SEVERITY_MEDIUM ? "MEDIUM"
                      : severity == GL_DEBUG_SEVERITY_LOW    ? "LOW"
                                                             : "INFO";

    if (severity == GL_DEBUG_SEVERITY_HIGH)
    {
        ++s_glHighSeverityErrorCount;
        s_glLastHighSeverityMessage =
            "type=0x" + std::to_string(type) + " id=" + std::to_string(id) + ": " + (message ? message : "(null)");
        LOG_ERROR(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
        return;
    }

    // non-high severity: deduplicate by id. the first sighting logs at full
    // severity; repeats increment a counter that the destructor later flushes
    // as a summary line.
    bool firstSighting = false;
    {
        std::lock_guard<std::mutex> lock(GetGLDedupMtx());
        auto& count = GetGLDedupCounts()[id];
        firstSighting = (count == 0);
        ++count;
    }
    if (!firstSighting)
    {
        return;
    }

    if (severity == GL_DEBUG_SEVERITY_MEDIUM)
    {
        LOG_WARN(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
    }
    else if (severity == GL_DEBUG_SEVERITY_LOW)
    {
        LOG_INFO(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
    }
    else
    {
        LOG_DEBUG(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
    }

    // fail fast in debug builds when a real gl api error fires.
    // high-severity GL_DEBUG_TYPE_ERROR means the driver caught a misuse such
    // as an invalid enum or an unbound program at draw time. synchronous
    // delivery already points the stack trace at the offending call, which
    // catches state-machine bugs before they turn into silent corruption.
    //
    // release builds, including RelWithDebInfo, only log the error.
#ifdef _DEBUG
    if (severity == GL_DEBUG_SEVERITY_HIGH && type == GL_DEBUG_TYPE_ERROR)
    {
#ifdef _MSC_VER
        __debugbreak();
#else
        __builtin_trap();
#endif
    }
#endif
}
} // namespace

QueueGLES32::QueueGLES32()
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        _triUsed[i] = false;
    }
    _usedCounter = 0;
    _vertexBufferUsed = 0;
    _indexBufferUsed = 0;
    _meshBase = 0;
    _meshSize = 0;
    _actTri = -1;
    _firstVertex = true;
    _firstIndex = true;
}

int QueueGLES32::Allocate(TextureGLES32* tex, int level, int spec, int minI, int maxI, int tip)
{
    int index = -1;
    if (tip >= minI && tip < maxI && _triUsed[tip])
    {
        TriQueue& triq = _tri[tip];
        if (tex == triq._texture && spec == triq._special)
            index = tip;
    }

    int free = -1;
    if (index < 0)
    {
        for (int i = minI; i < maxI; i++)
        {
            if (_triUsed[i])
            {
                TriQueue& triq = _tri[i];
                if (tex != triq._texture || spec != triq._special)
                    continue;
                index = i;
            }
            else if (free < 0)
            {
                free = i;
            }
        }
    }
    _usedCounter++;
    if (index >= 0)
    {
        TriQueue& triq = _tri[index];
        saturateMin(triq._level, level);
        triq._lastUsed = _usedCounter;
        return index;
    }
    if (free >= 0)
    {
        TriQueue& triq = _tri[free];
        triq._special = spec;
        triq._texture = tex;
        triq._level = level;
        triq._passId = SpecToPassId(spec);
        triq._lastUsed = _usedCounter;
        PoseidonAssert(triq._triangleQueue.Size() == 0);
        triq._triangleQueue.Resize(0);
        _triUsed[free] = true;
    }
    return free;
}

void QueueGLES32::Free(int i)
{
    PoseidonAssert(_tri[i]._triangleQueue.Size() == 0);
    PoseidonAssert(_triUsed[i]);
    _triUsed[i] = false;
}

EngineGLES32::EngineGLES32(int width, int height, bool windowed, int bpp)
{
    _w = width;
    _h = height;
    _windowed = windowed;
    _windowedRestoreW = width;
    _windowedRestoreH = height;
    _pixelSize = bpp;
    _depthBpp = 24;
    _refreshRate = 60;
    _bias = 0;
    _gamma = 1.0f;
    _frameOpen = false;
    _nightEye = 0;

    _grassParam[0] = 0;
    _grassParam[1] = 0;
    _grassParam[2] = 0;
    _grassParam[3] = 0;

    _clipANearEnabled = false;
    _clipAFarEnabled = false;

    _minGuardX = 0;
    _maxGuardX = _w;
    _minGuardY = 0;
    _maxGuardY = _h;

    _prepSpec = -1;
    _stencilExclusionEnabled = false;
    _texGenMode = TGFixed;
    _iOffset = 0;
    _lastQueueSource = nullptr;

    _lastClampU = false;
    _lastClampV = false;
    _pointSampling = false;
    _enableReorder = false;

    _texLoc = TexLocalVidMem;

    _pixelShaderSel = PSNone;
    _pixelShaderModeSel = PSMDay;
    _pixelShaderSpecularSel = PSSNormal;
    memset(_shaderProgram, 0, sizeof(_shaderProgram));

    _vertexShaderSel = VSNone;
    _formatSet = SingleTex;

    _textColor = Color(1, 1, 1, 1);

    LOG_INFO(Graphics, "GLES32: Initializing engine — bootstrap {}x{} {}bpp {} before display.cfg/user overrides", _w,
             _h, _pixelSize, _windowed ? "windowed" : "fullscreen");

    // sdl3 initialization.
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "GLES32: SDL_Init failed: {}", SDL_GetError());
        return;
    }

    // request the correct gl context profile for the platform.
#ifdef __ANDROID__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#else
    // request an OpenGL 3.3 core profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // the forward-compatible flag is required by Apple's gl implementation
    // for any 3.3 core context and is harmless on Windows or Linux.
    // the debug flag enables GL_CONTEXT_FLAG_DEBUG_BIT so KHR_debug callbacks
    // can fire. it stays enabled in all builds because RelWithDebInfo is the
    // main development configuration.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG | SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // the default framebuffer stays single-sampled on purpose. every frame
    // renders into the offscreen frame target, which carries the MSAA samples
    // and resolves into the window with a blit.
    // a multisampled window framebuffer would make the scaled resolve blit
    // illegal and would also make glReadPixels out of spec.

    // resolve the final placement from the engine config, the --window
    // override, and the target display's desktop mode.
    // borderless is forced to native desktop resolution at (0,0) so DWM can
    // recognize it and use independent flip.
    auto& engineCfg = GApp->GetConfig().GetEngineConfig();
    DisplayPlacementInput displayCfg;
    displayCfg.displayMode = engineCfg.displayMode;
    // --window forces windowed mode regardless of the saved display mode.
    // appConfig already normalizes --window during argument parsing, but keep
    // this path defensive.
#ifdef __ANDROID__
    displayCfg.displayMode = "borderless";
#else
    if (windowed && displayCfg.displayMode != "windowed")
        displayCfg.displayMode = "windowed";
    if (!windowed && displayCfg.displayMode == "windowed")
        displayCfg.displayMode = "borderless";
#endif
    displayCfg.width = _w;
    displayCfg.height = _h;

    int desktopW = 0, desktopH = 0, desktopRefresh = 0;
#ifdef __ANDROID__
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
    {
        desktopW = bounds.w;
        desktopH = bounds.h;
    }
#else
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay()))
    {
        desktopW = dm->w;
        desktopH = dm->h;
        desktopRefresh = (int)(dm->refresh_rate + 0.5f);
    }
#endif
    const WindowPlacement placement = ResolveWindowPlacement(displayCfg, desktopW, desktopH, desktopRefresh);
    _windowMode = placement.mode;

    // on Windows, borderless avoids SDL's fullscreen state machine because
    // Win11 and OpenGL can promote that path to exclusive on the first
    // SwapWindow.
    // on Linux and macOS we want the compositor's real desktop-fullscreen
    // state so shell work-area reservations do not treat the game as a normal
    // borderless window.
    //
    // windowed mode keeps the standard resizable bordered window.
    // SDL_WINDOW_HIGH_PIXEL_DENSITY opts into native-pixel rendering on
    // high-dpi displays. without it, SDL renders at logical pixels and blits
    // up, which makes the image blurry.
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    switch (placement.mode)
    {
        case WindowMode::Fullscreen:
        case WindowMode::Borderless:
            flags |= SDL_WINDOW_BORDERLESS;
            break;
        case WindowMode::Windowed:
            flags |= SDL_WINDOW_RESIZABLE;
            break;
    }

    _sdlWindow = SDL_CreateWindow("Poseidon [GL33]", placement.width, placement.height, flags);
    if (!_sdlWindow)
    {
        LOG_ERROR(Graphics, "GLES32: SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    if (placement.mode == WindowMode::Borderless)
    {
#ifndef _WIN32
        SDL_SetWindowFullscreenMode(_sdlWindow, nullptr);
        if (!SDL_SetWindowFullscreen(_sdlWindow, true))
            LOG_WARN(Graphics, "GLES32: SDL_SetWindowFullscreen(true) failed for borderless startup: {}", SDL_GetError());
#else
        if (placement.posX != WindowPlacement::kCentered)
            SDL_SetWindowPosition(_sdlWindow, placement.posX, placement.posY);
#endif
    }
    else if (placement.posX != WindowPlacement::kCentered)
    {
        SDL_SetWindowPosition(_sdlWindow, placement.posX, placement.posY);
    }

    _w = placement.width;
    _h = placement.height;
    if (placement.refreshHz > 0)
        _refreshRate = placement.refreshHz;

    // create the gl context.
    _glContext = SDL_GL_CreateContext(_sdlWindow);
    if (!_glContext)
    {
        LOG_ERROR(Graphics, "GLES32: SDL_GL_CreateContext failed: {}", SDL_GetError());
        return;
    }

    // load gl function pointers via glad.
    if (!gladLoadGLES2((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        LOG_ERROR(Graphics, "GLES32: gladLoadGLES2 failed");
        _glContext = nullptr;
        return;
    }

    // vsync is enabled by default. the options menu can override it at
    // runtime through SetSwapInterval() and GetSwapInterval().
    SDL_GL_SetSwapInterval(1);

    // initialize the imgui debug overlay. it stays hidden by default and F8
    // toggles it. the GL context must already exist.
    DebugOverlay::Init(_sdlWindow, _glContext);

    // msaa lives on the offscreen frame target; _msaaActive is set when that
    // target is created with multisample storage. see ApplyPendingRenderScale.

    // khr_debug callback: turn raw gl errors into actionable per-call log
    // lines instead of stale error sweeps later.
    // synchronous output ensures the callback fires inside the call site that
    // produced the error.
    {
        GLint ctxFlags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &ctxFlags);
        if ((ctxFlags & GL_CONTEXT_FLAG_DEBUG_BIT) && glDebugMessageCallback)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(GlDebugCallback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
#ifdef __ANDROID__
            // silence extremely noisy adreno performance warnings.
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW, 0, nullptr, GL_FALSE);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
#endif
            LOG_INFO(Graphics, "GLES32: KHR_debug callback wired (synchronous)");
        }
        else
        {
            LOG_INFO(Graphics, "GLES32: KHR_debug unavailable — debug context: {}, callback fn: {}",
                     (ctxFlags & GL_CONTEXT_FLAG_DEBUG_BIT) ? "yes" : "no",
                     glDebugMessageCallback ? "available" : "missing");
        }
    }

#ifdef __ANDROID__
    // use the actual SurfaceView or window-placement dimensions to avoid size
    // mismatches with the compositor.
    // SDL_GetWindowSizeInPixels can return the safe-area size on startup, which
    // does not always match the full-screen SurfaceView or EGL backing surface.
    _w = placement.width;
    _h = placement.height;
#else
    // query the actual pixel dimensions.
    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    _w = cw;
    _h = ch;
#endif

    // vendor and renderer make hybrid-gpu reports self-diagnosing because they
    // show which GPU the platform actually handed the process.
    LOG_INFO(Graphics, "GLES32: OpenGL {} — {} — {}", (const char*)glGetString(GL_VERSION),
             (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER));
    LOG_INFO(Graphics, "GLES32: surface resolved to {}x{} {}", _w, _h, _windowed ? "windowed" : "fullscreen");

#ifdef __ANDROID__
    // query DXT support dynamically on android.
    if (SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc"))
    {
        _dxtFormats = 0x3E; // DXT1..DXT5.
        LOG_INFO(Graphics, "GLES32: Hardware S3TC (DXT) texture compression supported by driver.");
    }
    else
    {
        _dxtFormats = 0;
        LOG_INFO(Graphics, "GLES32: Hardware S3TC (DXT) texture compression NOT supported. Software fallback will be used (expect performance/visual artifacts).");
    }
#endif

    // hook SDL events to the engine.
    _eventWindow.Attach(_sdlWindow, _w, _h);

    // initialize shaders, vertex buffers, and 3d state. InitGL also sets the
    // gl viewport; the splash or progress UI handles startup feedback.
    LoadConfig();
    InitGL();
}

EngineGLES32::ShutdownGuard::~ShutdownGuard()
{
    // I-05 / B-019: clear the base class FontCache while engine->_textBank is
    // still alive.
    // this destructor runs first in the EngineGLES32 teardown chain, so
    // _textBank and the other EngineGLES32 members still have valid storage.
    // by the time the base ~Engine() destroys _fonts, it is empty and the
    // dangling-Ref<Texture> path of B-019 cannot fire.
    if (engine)
        engine->ClearFontCache();
}

EngineGLES32::~EngineGLES32()
{
    LOG_INFO(Graphics, "GLES32: Destroying engine");

    SaveConfig();

    DebugOverlay::Shutdown();

    ShutdownGL();

    _eventWindow.Detach();

    if (_glContext)
    {
        SDL_GL_DestroyContext((SDL_GLContext)_glContext);
        _glContext = nullptr;
    }

    if (_sdlWindow)
    {
        SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
    }
}
