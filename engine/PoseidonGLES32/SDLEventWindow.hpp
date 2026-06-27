#pragma once
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <SDL3/SDL.h>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>

// SDL input buffer functions from InputProcessing_sdl.cpp.
extern void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp);
extern void SDLInput_BufferMouseButton(int btn, bool down);
extern void SDLInput_BufferMouseMotion(float dx, float dy);
extern void SDLInput_BufferMouseWheel(float dy);
extern void SDLInput_GamepadAdded(SDL_JoystickID which);
extern void SDLInput_GamepadRemoved(SDL_JoystickID which);
extern void SDLInput_BufferUIKeyEvent(SDL_Keycode key, bool down);
extern void SDLInput_BufferUICharEvent(const char* text);
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
extern void SetSkipKeys(bool skip);
#ifdef __ANDROID__
extern void SDLInput_SetAbsoluteCursor(float x, float y);
extern void SDLInput_BufferMouseWheel(float dy);

enum {
    VB_WASD = 1,
    VB_LOOK = 2,
    VB_FIRE_LOOK = 3,
    VB_ADS_LOOK = 4,
    VB_ACTION = 5,
    VB_KEY = 6,
    VB_ZOOM_LOOK = 7
};

struct VirtualButtonDef {
    const char* name;
    float x, y, r;
    int type;
    int data; 
};

extern VirtualButtonDef g_mobileButtons[];
extern const int g_numMobileButtons;

struct VirtualTouchRenderState {
    bool active;
    float currX, currY;
};
extern VirtualTouchRenderState g_mobileRenderState[32];
#endif

// SDL event-pump helper used by EngineGLES32.
// it does not own the SDL_Window; the renderer manages the window lifecycle.
// handles event polling, focus tracking, and input forwarding.
class SDLEventWindow
{
    SDL_Window* _sdlWindow = nullptr;
    int _width = 0, _height = 0;
    bool _open = false, _resized = false;
    bool _focused = true, _focusGained = false, _focusLost = false;
    bool _mouseGrab = true;
    bool _altEnterConsumed = false;
    bool _fullscreenTransitioning = false; // blocks phantom Alt+Enter during transitions.

    struct TouchState {
        SDL_FingerID id = -1;
        float startX = 0.0f, startY = 0.0f;
        float lastX = 0.0f, lastY = 0.0f;
        bool isMoving = false;
        
        int buttonId = -1;
        bool isLook = false;
        bool isMove = false;
        bool isAction = false;
        float actionScrollAccum = 0.0f;
        
        bool moveW = false, moveA = false, moveS = false, moveD = false;
    };
    TouchState _touches[10];

    TouchState* GetTouch(SDL_FingerID id) {
        for (int i = 0; i < 10; ++i) {
            if (_touches[i].id == id) return &_touches[i];
        }
        return nullptr;
    }

    TouchState* AllocTouch(SDL_FingerID id) {
        for (int i = 0; i < 10; ++i) {
            if (_touches[i].id == -1) {
                _touches[i].id = id;
                return &_touches[i];
            }
        }
        return nullptr;
    }

    void ReleaseTouch(TouchState* t) {
        if (!t) return;
        t->id = -1;
        if (t->moveW) { SDLInput_BufferKeyEvent(SDL_SCANCODE_W, false, Poseidon::Foundation::GlobalTickCount()); t->moveW = false; }
        if (t->moveA) { SDLInput_BufferKeyEvent(SDL_SCANCODE_A, false, Poseidon::Foundation::GlobalTickCount()); t->moveA = false; }
        if (t->moveS) { SDLInput_BufferKeyEvent(SDL_SCANCODE_S, false, Poseidon::Foundation::GlobalTickCount()); t->moveS = false; }
        if (t->moveD) { SDLInput_BufferKeyEvent(SDL_SCANCODE_D, false, Poseidon::Foundation::GlobalTickCount()); t->moveD = false; }
        t->isMove = t->isLook = t->isMoving = false;
    }

  public:
    // attach to an existing SDL window without taking ownership.
    // marks the app active and acquires the mouse.
    void Attach(SDL_Window* window, int w, int h)
    {
        _sdlWindow = window;
        _width = w;
        _height = h;
        _open = (window != nullptr);
        _focused = true;

        if (_sdlWindow)
        {
            if (_mouseGrab)
                SDL_SetWindowRelativeMouseMode(_sdlWindow, true);
            SDL_StartTextInput(_sdlWindow);
        }

        extern void SetMouseAcquired(bool acquired);
        SetMouseAcquired(true);
        GApp->m_appActive = true;
    }

    void Detach()
    {
        _sdlWindow = nullptr;
        _open = false;
    }

    // init is unused because the renderer creates the window.
    bool Init(int, int, bool) { return false; }

    int GetWidth() const { return _width; }
    int GetHeight() const { return _height; }
    void SwapBuffers() {} // the renderer handles present.

    void HandleEvents()
    {
        _resized = false;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // imgui needs every event so it can update its input state.
            // forwarding is harmless when the overlay is hidden.
            Poseidon::Dev::DebugOverlay::ProcessEvent(event);

            // when the imgui panel is focused over a slider or text input,
            // swallow the matching SDL events so they do not also move the
            // player or fire the menu cursor.
            // always allow window lifecycle events through, and always allow F8
            // so the panel can dismiss itself.
            const bool isKeyPress = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
            const bool isKey = isKeyPress || event.type == SDL_EVENT_TEXT_INPUT;
            const bool isMouse = event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                 event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
            // read event.key only for real key events; on TEXT_INPUT the active
            // union member is event.text, so event.key.scancode is undefined.
            const bool isF8 = isKeyPress && event.key.scancode == SDL_SCANCODE_F8;
            if (!isF8 && ((isKey && Poseidon::Dev::DebugOverlay::WantsKeyboard()) ||
                          (isMouse && Poseidon::Dev::DebugOverlay::WantsMouse())))
            {
                continue;
            }
            
#ifdef __ANDROID__
            if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) continue;
            if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.which == SDL_TOUCH_MOUSEID) continue;
#endif

            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                // Alt+F4 is a legitimate in-game combo, with Alt used for freelook
                // and F4 used to select unit 4.
                // on Windows the OS turns Alt+F4 into a window-close request; ignore
                // it only during active gameplay so the F4 keypress reaches the game.
                // everywhere else Alt+F4 is the standard desktop quit and must close,
                // as do the title-bar X, taskbar, and menu Quit actions.
                const bool altDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
                if (!::Poseidon::ShouldHonorWindowClose(altDown, GApp->IsInGameplay()))
                {
                    LOG_INFO(Input, "SDLEventWindow: ignoring Alt+F4 close (valid in-game shortcut)");
                    continue;
                }
                _open = false;
                GApp->m_closeRequest = true;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
#ifdef __ANDROID__
                // on android, SDL_GetWindowSizeInPixels can still report the
                // safe-area height until the EGL surface is fully recreated.
                // read the display bounds for the authoritative physical resolution.
                if (_sdlWindow)
                {
                    SDL_DisplayID _evDisplay = SDL_GetDisplayForWindow(_sdlWindow);
                    if (!_evDisplay) _evDisplay = SDL_GetPrimaryDisplay();
                    SDL_Rect _evBounds{};
                    if (SDL_GetDisplayBounds(_evDisplay, &_evBounds) && _evBounds.w > 0 && _evBounds.h > 0)
                    {
                        _width  = _evBounds.w;
                        _height = _evBounds.h;
                    }
                    else
                    {
                        SDL_GetWindowSizeInPixels(_sdlWindow, &_width, &_height);
                    }
                }
#else
                if (_sdlWindow)
                    SDL_GetWindowSizeInPixels(_sdlWindow, &_width, &_height);
#endif
                _resized = true;
                // notify the engine so it can resize the swap chain with the final
                // dimensions.
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->OnWindowResized(_width, _height);
            }
            else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)
            {
                _fullscreenTransitioning = false;
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->OnFullscreenChanged(false);
            }
            else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
            {
                _fullscreenTransitioning = false;
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->OnFullscreenChanged(true);
            }
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
            {
                _focused = true;
                _focusGained = true;
                GApp->m_appActive = true;
                if (_mouseGrab && _sdlWindow)
                    SDL_SetWindowRelativeMouseMode(_sdlWindow, true);
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->Activate();
                SetSkipKeys(true);
                if (Poseidon::GSoundsys)
                    Poseidon::GSoundsys->Activate(true);
            }
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                _focused = false;
                _focusLost = true;
                GApp->m_appActive = false;
                if (_sdlWindow)
                    SDL_SetWindowRelativeMouseMode(_sdlWindow, false);
                if (!GApp->m_keepFocus)
                {
                    if (::Poseidon::GEngine)
                        ::Poseidon::GEngine->Deactivate();
                    SetSkipKeys(true);
                    if (Poseidon::GSoundsys)
                        Poseidon::GSoundsys->Activate(false);
                }
            }
            else if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
            {
                GApp->m_appPaused = true;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
            {
                GApp->m_appPaused = false;
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (!event.key.repeat && event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT))
                {
                    if (_fullscreenTransitioning)
                    {
                        LOG_INFO(Graphics, "SDLEventWindow: Alt+Enter ignored (transition in progress)");
                        _altEnterConsumed = true;
                        continue;
                    }
                    if (::Poseidon::GEngine && _sdlWindow)
                    {
                        bool windowed = ::Poseidon::GEngine->IsWindowed();
                        LOG_INFO(Graphics, "SDLEventWindow: Alt+Enter requesting {}",
                                 windowed ? "fullscreen" : "windowed");
                        _fullscreenTransitioning = true;
                        ::Poseidon::GEngine->SetWindowMode(windowed ? ::Poseidon::WindowMode::Borderless
                                                                    : ::Poseidon::WindowMode::Windowed);
                        // the borderless and windowed paths in SetWindowMode are
                        // synchronous, so they do not go through SDL's fullscreen
                        // state machine.
                        // clear the flag here for those modes so the next Alt+Enter
                        // is not swallowed as a transition in progress.
                        // exclusive fullscreen still goes through SDL's state machine
                        // and clears the flag from its ENTER_FULLSCREEN handler.
                        _fullscreenTransitioning = false;
                    }
                    _altEnterConsumed = true;
                    continue;
                }
                if (!event.key.repeat)
                    SDLInput_BufferKeyEvent(event.key.scancode, true, Poseidon::Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, true);
            }
            else if (event.type == SDL_EVENT_KEY_UP)
            {
                if (event.key.scancode == SDL_SCANCODE_RETURN && _altEnterConsumed)
                {
                    _altEnterConsumed = false;
                    continue;
                }
                SDLInput_BufferKeyEvent(event.key.scancode, false, Poseidon::Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, false);
            }
            else if (event.type == SDL_EVENT_TEXT_INPUT)
                SDLInput_BufferUICharEvent(event.text.text);
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                int btn = event.button.button - 1;
                if (btn == 1)
                    btn = 2;
                else if (btn == 2)
                    btn = 1;
                SDLInput_BufferMouseButton(btn, true);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                int btn = event.button.button - 1;
                if (btn == 1)
                    btn = 2;
                else if (btn == 2)
                    btn = 1;
                SDLInput_BufferMouseButton(btn, false);
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION)
                SDLInput_BufferMouseMotion(event.motion.xrel, event.motion.yrel);
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
                SDLInput_BufferMouseWheel(event.wheel.y);
            else if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                SDLInput_GamepadAdded(event.gdevice.which);
            else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
                SDLInput_GamepadRemoved(event.gdevice.which);
            else if (event.type == SDL_EVENT_FINGER_DOWN)
            {
                TouchState* t = AllocTouch(event.tfinger.fingerID);
                if (t) {
                    t->startX = event.tfinger.x;
                    t->startY = event.tfinger.y;
                    t->lastX = event.tfinger.x;
                    t->lastY = event.tfinger.y;
                    t->buttonId = -1;
                    t->isMoving = false;
                    t->isLook = false;
                    t->isMove = false;
                    t->isAction = false;
                    t->actionScrollAccum = 0.0f;
                    
                    bool inGameplay = GApp && GApp->IsInGameplay();
#ifdef __ANDROID__
                    float closestDist = 999.0f;
                    int closestBtn = -1;
                    for (int i=0; i<g_numMobileButtons; ++i) {
                        if (!inGameplay && g_mobileButtons[i].data != SDL_SCANCODE_ESCAPE) continue;
                        
                        float dx = event.tfinger.x - g_mobileButtons[i].x;
                        float dy = (event.tfinger.y - g_mobileButtons[i].y) * _height / (float)_width;
                        float distSq = dx*dx + dy*dy;
                        if (distSq < g_mobileButtons[i].r * g_mobileButtons[i].r) {
                            if (distSq < closestDist) {
                                closestDist = distSq;
                                closestBtn = i;
                            }
                        }
                    }
                    
                    if (closestBtn != -1) {
                        t->buttonId = closestBtn;
                        int type = g_mobileButtons[closestBtn].type;
                        
                        g_mobileRenderState[closestBtn].active = true;
                        g_mobileRenderState[closestBtn].currX = event.tfinger.x;
                        g_mobileRenderState[closestBtn].currY = event.tfinger.y;
                        
                        if (type == VB_WASD) t->isMove = true;
                        else if (type == VB_LOOK) t->isLook = true;
                        else if (type == VB_FIRE_LOOK) { t->isLook = true; SDLInput_BufferMouseButton(0, true); }
                        else if (type == VB_ADS_LOOK) { t->isLook = true; SDLInput_BufferMouseButton(1, true); }
                        else if (type == VB_ZOOM_LOOK) { t->isLook = true; SDLInput_BufferKeyEvent(SDL_SCANCODE_KP_PLUS, true, Poseidon::Foundation::GlobalTickCount()); }
                        else if (type == VB_KEY) { SDLInput_BufferKeyEvent((SDL_Scancode)g_mobileButtons[closestBtn].data, true, Poseidon::Foundation::GlobalTickCount()); }
                        else if (type == VB_ACTION) { t->isAction = true; }
                    } else if (!inGameplay) {
                        // Menus: Map absolute position
                        SDLInput_SetAbsoluteCursor(event.tfinger.x, event.tfinger.y);
                        SDLInput_BufferMouseButton(0, true);
                    } else {
                        if (event.tfinger.x < 0.5f) t->isMove = true;
                        else t->isLook = true;
                    }
#else
                    if (inGameplay) {
                        if (event.tfinger.x < 0.5f) t->isMove = true;
                        else t->isLook = true;
                    } else {
                        SDLInput_BufferMouseButton(0, true);
                    }
#endif
                }
            }
            else if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
            {
                TouchState* t = GetTouch(event.tfinger.fingerID);
                if (t) {
                    bool inGameplay = GApp && GApp->IsInGameplay();
                    
                    if (inGameplay && t->isLook && !t->isMoving && t->buttonId == -1) {
                        // a tap on the right side maps to a left click.
                        SDLInput_BufferMouseButton(0, true);
                        SDLInput_BufferMouseButton(0, false);
                    }
#ifdef __ANDROID__
                    if (t->buttonId != -1) {
                        g_mobileRenderState[t->buttonId].active = false;
                        int type = g_mobileButtons[t->buttonId].type;
                        if (type == VB_FIRE_LOOK) SDLInput_BufferMouseButton(0, false);
                        else if (type == VB_ADS_LOOK) SDLInput_BufferMouseButton(1, false);
                        else if (type == VB_ZOOM_LOOK) SDLInput_BufferKeyEvent(SDL_SCANCODE_KP_PLUS, false, Poseidon::Foundation::GlobalTickCount());
                        else if (type == VB_KEY) SDLInput_BufferKeyEvent((SDL_Scancode)g_mobileButtons[t->buttonId].data, false, Poseidon::Foundation::GlobalTickCount());
                        else if (type == VB_ACTION && !t->isMoving) {
                            SDLInput_BufferKeyEvent(SDL_SCANCODE_RETURN, true, Poseidon::Foundation::GlobalTickCount());
                            SDLInput_BufferKeyEvent(SDL_SCANCODE_RETURN, false, Poseidon::Foundation::GlobalTickCount());
                        }
                    }
#endif
                    if (inGameplay && t->isMove) {
                        if (t->moveW) { SDLInput_BufferKeyEvent(SDL_SCANCODE_W, false, Poseidon::Foundation::GlobalTickCount()); t->moveW = false; }
                        if (t->moveA) { SDLInput_BufferKeyEvent(SDL_SCANCODE_A, false, Poseidon::Foundation::GlobalTickCount()); t->moveA = false; }
                        if (t->moveS) { SDLInput_BufferKeyEvent(SDL_SCANCODE_S, false, Poseidon::Foundation::GlobalTickCount()); t->moveS = false; }
                        if (t->moveD) { SDLInput_BufferKeyEvent(SDL_SCANCODE_D, false, Poseidon::Foundation::GlobalTickCount()); t->moveD = false; }
                    }
                    
                    if (t->buttonId == -1 && !inGameplay) {
                        SDLInput_BufferMouseButton(0, false);
                    }
                    ReleaseTouch(t);
                }
            }
            else if (event.type == SDL_EVENT_FINGER_MOTION)
            {
                TouchState* t = GetTouch(event.tfinger.fingerID);
                if (t) {
                    float dx = event.tfinger.x - t->lastX;
                    float dy = event.tfinger.y - t->lastY;
                    t->lastX = event.tfinger.x;
                    t->lastY = event.tfinger.y;
                    
                    float distX = event.tfinger.x - t->startX;
                    float distY = event.tfinger.y - t->startY;
                    float distSq = distX * distX + distY * distY;
                    if (distSq > 0.0004f) { // roughly 0.02f squared.
                        t->isMoving = true;
                    }
                    
                    if (GApp && GApp->IsInGameplay()) {
#ifdef __ANDROID__
                        if (t->buttonId != -1) {
                            g_mobileRenderState[t->buttonId].currX = event.tfinger.x;
                            g_mobileRenderState[t->buttonId].currY = event.tfinger.y;
                        }
                        
                        if (t->isAction) {
                            float scroll = dy * 30.0f; 
                            t->actionScrollAccum -= scroll;
                            if (t->actionScrollAccum > 1.0f) { SDLInput_BufferMouseWheel(1.0f); t->actionScrollAccum -= 1.0f; }
                            if (t->actionScrollAccum < -1.0f) { SDLInput_BufferMouseWheel(-1.0f); t->actionScrollAccum += 1.0f; }
                        }
#endif
                        if (t->isLook) {
                            float speedX = _width * 1.5f; // sensitivity multiplier.
                            float speedY = _height * 1.5f;
                            SDLInput_BufferMouseMotion(dx * speedX, dy * speedY);
                        } else if (t->isMove && t->isMoving) {
                            float localY = distY;
                            float localX = distX;
#ifdef __ANDROID__
                            if (t->buttonId != -1) {
                                localY = event.tfinger.y - g_mobileButtons[t->buttonId].y;
                                localX = event.tfinger.x - g_mobileButtons[t->buttonId].x;
                            }
#endif
                            bool wantW = localY < -0.05f;
                            bool wantS = localY > 0.05f;
                            bool wantA = localX < -0.05f;
                            bool wantD = localX > 0.05f;
                            
                            if (wantW != t->moveW) { SDLInput_BufferKeyEvent(SDL_SCANCODE_W, wantW, Poseidon::Foundation::GlobalTickCount()); t->moveW = wantW; }
                            if (wantA != t->moveA) { SDLInput_BufferKeyEvent(SDL_SCANCODE_A, wantA, Poseidon::Foundation::GlobalTickCount()); t->moveA = wantA; }
                            if (wantS != t->moveS) { SDLInput_BufferKeyEvent(SDL_SCANCODE_S, wantS, Poseidon::Foundation::GlobalTickCount()); t->moveS = wantS; }
                            if (wantD != t->moveD) { SDLInput_BufferKeyEvent(SDL_SCANCODE_D, wantD, Poseidon::Foundation::GlobalTickCount()); t->moveD = wantD; }
                        }
                    } else if (t->buttonId == -1) {
                        // Dragging in menus moves absolute cursor
#ifdef __ANDROID__
                        SDLInput_SetAbsoluteCursor(event.tfinger.x, event.tfinger.y);
#endif
                    }
                }
            }
        }
    }

    bool IsOpen() const { return _open; }
    void* GetNativeHandle() const { return _sdlWindow; }
    bool WasResized()
    {
        bool r = _resized;
        _resized = false;
        return r;
    }
    bool HasFocus() const { return _focused; }
    bool ConsumeGainedFocus()
    {
        bool r = _focusGained;
        _focusGained = false;
        return r;
    }
    bool ConsumeLostFocus()
    {
        bool r = _focusLost;
        _focusLost = false;
        return r;
    }
    void SetMouseGrab(bool grab)
    {
        _mouseGrab = grab;
        if (_sdlWindow)
            SDL_SetWindowRelativeMouseMode(_sdlWindow, grab && _focused);
    }
    bool IsMouseGrabbed() const { return _mouseGrab; }
    void SetTitle(const char* title)
    {
        if (_sdlWindow)
            SDL_SetWindowTitle(_sdlWindow, title);
    }
};
