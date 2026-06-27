#include <PoseidonGLES32/EngineGLES32.hpp>
#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <Poseidon/Graphics/Core/ZBiasMath.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>

#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif

void EngineGLES32::SetFogColor(ColorVal /*fog*/)
{
    if (!_glContext)
        return;

    // keep the cached frame state in sync so subsequent frame constant
    // uploads preserve the updated fog color.
    _frameState.fogColor[0] = _fogColor.R();
    _frameState.fogColor[1] = _fogColor.G();
    _frameState.fogColor[2] = _fogColor.B();
    _frameState.fogColor[3] = 1.0f;

    UploadPSFogColor(_fogColor);
}

void EngineGLES32::DoSetGamma()
{
#ifdef _WIN32
    if (!_sdlWindow)
        return;

    SDL_PropertiesID props = SDL_GetWindowProperties(_sdlWindow);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd)
        return;

    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return;

    WORD ramp[3][256];
    float eGamma = 1.0f / _gamma;
    ramp[0][0] = ramp[1][0] = ramp[2][0] = 0;

    for (int i = 1; i < 256; i++)
    {
        float x = i * (1.0f / 255.0f);
        float fx = powf(x, eGamma);
        int ifx = static_cast<int>(fx * 65535.0f);
        if (ifx < 0)
            ifx = 0;
        if (ifx > 65535)
            ifx = 65535;
        ramp[0][i] = ramp[1][i] = ramp[2][i] = static_cast<WORD>(ifx);
    }

    SetDeviceGammaRamp(hdc, ramp);
    ReleaseDC(hwnd, hdc);

    LOG_DEBUG(Graphics, "GLES32: set gamma {:.3f}", _gamma);
#endif
}

void EngineGLES32::SetGamma(float gamma)
{
    saturate(gamma, 1e-3f, 1e3f);
    _gamma = gamma;

    if (_sdlWindow)
    {
        DoSetGamma();
    }
}

void EngineGLES32::SetBias(int bias)
{
    if (bias == _bias)
        return;

    _bias = bias;

    if (IsIn3DPass())
    {
        // flush queued draws before updating the projection so all pending
        // geometry is rendered with the previous depth bias.
        FlushAndFreeAllQueues(_queueNo, true);

        Camera* camera = GScene->GetCamera();
        int projBias = _canZBias ? 0 : _bias;
        ConvertProjectionMatrix(_frameState.projection, camera->ProjectionNormal(), projBias);
        UploadVSProjection(_frameState);
    }
}

// shadow pass boundaries flush queued draws before switching between
// regular and shadow rendering.
void EngineGLES32::BeginShadowPass()
{
    FlushAndFreeAllQueues(_queueNo, /*nonEmptyOnly*/ true);
}

void EngineGLES32::EndShadowPass()
{
    FlushAndFreeAllQueues(_queueNo, /*nonEmptyOnly*/ true);
}

void EngineGLES32::GetZCoefs(float& zAdd, float& zMult)
{
    const auto c = Poseidon::render::zbias::SoftwareCoefs(_bias);
    zMult = c.zMult;
    zAdd = c.zAdd;
}

bool EngineGLES32::CanZBias() const
{
    // always use the software z-bias path to match the d3d11 renderer.
    return false;
}

void EngineGLES32::SetGrassParams(float a1, float a2, float a3, float a4)
{
    if (fabs(_grassParam[0] - a1) < 0.001 && fabs(_grassParam[1] - a2) < 0.001 && fabs(_grassParam[2] - a3) < 0.001 &&
        fabs(_grassParam[3] - a4) < 0.001)
    {
        return;
    }

    _grassParam[0] = a1;
    _grassParam[1] = a2;
    _grassParam[2] = a3;
    _grassParam[3] = a4;

    if (_pixelShaderSel == PSGrass)
    {
        DoSetGrassParamsPS();
    }
}

void EngineGLES32::DoSetGrassParamsPS()
{
    _psConstants.grassCoef1[0] = 0;
    _psConstants.grassCoef1[1] = 0;
    _psConstants.grassCoef1[2] = 0;
    _psConstants.grassCoef1[3] = _grassParam[0];

    _psConstants.grassCoef2[0] = 0;
    _psConstants.grassCoef2[1] = 0;
    _psConstants.grassCoef2[2] = 0;
    _psConstants.grassCoef2[3] = _grassParam[1];

    UploadPSConstant(PSConstants::SlotGrassCoef1, _psConstants.grassCoef1);
    UploadPSConstant(PSConstants::SlotGrassCoef2, _psConstants.grassCoef2);
}
