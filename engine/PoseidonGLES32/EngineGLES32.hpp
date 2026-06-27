#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGINE_GLES32_HPP
#define __ENGINE_GLES32_HPP

using namespace Poseidon;
class TextureGLES32;
class TextBankGLES32;

#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Core/RenderState.hpp>

// the gles backend does not expose d3d profiling scopes.
#define PROFILE_DX_SCOPE(name)

#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <PoseidonGLES32/SDLEventWindow.hpp>

enum PixelShaderSpecular
{
    PSSSpecular,
    PSSNormal,
    NPixelShaderSpecular
};

#include <PoseidonGLES32/GLESCompat.hpp>
#include <SDL3/SDL.h>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>

typedef int ZFyzType;
#define zToFyz(z) toInt(z * 0x10000000)
#define MAX_FYZ_Z 0x7fffffff

enum TexLoc
{
    TexLocalVidMem,
    TexNonlocalVidMem,
    TexSysMem
};

enum
{
    MeshBufferLength = 32 * 1024,
    IndexBufferLength = 4 * 1024
};

enum PixelShaderMode
{
    PSMDay,
    PSMNight,
    NPixelShaderModes
};

enum PixelShaderID
{
    PSNormal,
    PSDetail,
    PSGrass,
    PSWater,
    PSFlat,
    PSShadow, // unlit cutout shader using constant black rgb and texture alpha.
    NPixelShaders,
    PSNone = NPixelShaders
};

struct alignas(16) PSConstants
{
    enum Slot : int
    {
        SlotFogColor = 0,
        SlotAlphaRef = 1,
        SlotConstColor = 3,
        SlotLightDir = 4,
        SlotGrassCoef1 = 5,
        SlotGrassCoef2 = 6,
        SlotRgbEyeCoef = 7
    };

    float fogColor[4] = {0, 0, 0, 1};
    float alphaRef[4] = {0, 0, 0, 0};
    float lightDir[4] = {0, 0, 0, 0};
    float grassCoef1[4] = {0, 0, 0, 0};
    float grassCoef2[4] = {0, 0, 0, 0};
    float rgbEyeCoef[4] = {0, 0, 0, 1};
    // per-object tint. white is the identity value and uploads to slotconstcolor.
    // keep this field last so the legacy memory layout stays stable.
    float constColor[4] = {1, 1, 1, 1};
};

enum VertexShaderID
{
    VSScreen,
    VSTransform,
    VSShadow, // unlit transform used by the shadow depth pass.
    NVertexShaders,
    VSNone = NVertexShaders
};

namespace VSConst
{
// vertex shader uniform buffer layout. each slot covers one vec4.
// uniform ranges do not overlap, so state updates stay isolated.
// vpscale and proj stay in separate slots so 2d and 3d projection state cannot clobber each other.
enum : int
{
    SlotProj = 0,
    SlotView = 4,
    SlotWorld = 8,
    SlotSunDir = 12,
    SlotAmbient = 13,
    SlotDiffuse = 14,
    SlotEmissive = 15,
    SlotFogParam = 16,
    SlotCamPos = 17,
    SlotSpecular = 18,
    SlotSpecEn = 19,
    SlotSunEn = 20,
    SlotVpScale = 21,
    // slots 22..23 stay unused.
    SlotTexMat0 = 24,
    SlotTexMat1 = 28,
    SlotTexCtrl = 32,
    // local light state for per-vertex night illumination.
    SlotLightCount = 33,   // x component stores the active local-light count.
    SlotLightPos = 34,     // maxlocallights * vec4: xyz stores world position, w stores start attenuation.
    SlotLightDiffuse = 42, // maxlocallights * vec4: diffuse contribution after the night multiplier.
    SlotLightAmbient = 50, // maxlocallights * vec4: ambient contribution after the night multiplier.
    SlotLightDir = 58,     // maxlocallights * vec4: xyz stores world direction, w stores the spot flag.
    SlotLightVP = 66,      // 4x vec4: light view-projection matrix used for shadow-map sampling.
};

// maximum number of local lights processed per vertex shader invocation.
static constexpr int MaxLocalLights = 8;

// static assertions verify that uniform buffer register ranges do not overlap.
// add any new slot definitions here and extend the checks at the same time.
static_assert(SlotView >= SlotProj + 4, "SlotView overlaps SlotProj");
static_assert(SlotWorld >= SlotView + 4, "SlotWorld overlaps SlotView");
static_assert(SlotSunDir >= SlotWorld + 4, "SlotSunDir overlaps SlotWorld");
static_assert(SlotVpScale >= SlotSunEn + 1, "SlotVpScale overlaps SlotSunEn");
static_assert(SlotTexMat0 >= SlotVpScale + 1, "SlotTexMat0 overlaps SlotVpScale");
static_assert(SlotTexMat1 >= SlotTexMat0 + 4, "SlotTexMat1 overlaps SlotTexMat0");
static_assert(SlotTexCtrl >= SlotTexMat1 + 4, "SlotTexCtrl overlaps SlotTexMat1");
static_assert(SlotLightCount >= SlotTexCtrl + 1, "SlotLightCount overlaps SlotTexCtrl");
static_assert(SlotLightPos >= SlotLightCount + 1, "SlotLightPos overlaps SlotLightCount");
static_assert(SlotLightDiffuse >= SlotLightPos + MaxLocalLights, "SlotLightDiffuse overlaps SlotLightPos");
static_assert(SlotLightAmbient >= SlotLightDiffuse + MaxLocalLights, "SlotLightAmbient overlaps SlotLightDiffuse");
static_assert(SlotLightDir >= SlotLightAmbient + MaxLocalLights, "SlotLightDir overlaps SlotLightAmbient");
static_assert(SlotLightVP >= SlotLightDir + MaxLocalLights, "SlotLightVP overlaps SlotLightDir");
}; // namespace VSConst

struct TriQueue
{
    StaticArray<WORD> _triangleQueue;

    TextureGLES32* _texture;
    int _level;
    int _special;
    Poseidon::PassId _passId = Poseidon::PassId::Opaque;
    int _lastUsed;
};

enum
{
    MaxTriQueues = 32,
    TriQueueSize = 2048
};

struct QueueGLES32
{
    int _vertexBufferUsed;
    int _indexBufferUsed;

    int _meshBase, _meshSize;

    TriQueue _tri[MaxTriQueues];
    bool _triUsed[MaxTriQueues];
    int _actTri;

    int _usedCounter;
    bool _firstVertex;
    bool _firstIndex;

    QueueGLES32();
    int Allocate(TextureGLES32* tex, int level, int spec, int minI, int maxI, int tip);
    void Free(int i);
};

struct SVertex
{
    Vector3P pos;
    Vector3P norm;
    Poseidon::UVPair t0;
};

// shader hot-reload override directory.
// when set, the shader compiler loads .glsl files from this path instead of embedded strings.
void SetShaderOverrideDir(const std::string& dir);

class EngineGLES32 : public Engine
{
    typedef Engine base;

  protected:
    int _w = 0, _h = 0; // swapchain backbuffer dimensions.
    bool _resetNeeded = false;
    TLVertexTable* _mesh = nullptr; // active transformed and lit vertex data for the current draw.

    enum RenderMode
    {
        RMLines,
        RMTris,
        RM2DLines,
        RM2DTris
    };
    RenderMode _renderMode = RMTris;

    bool _sunEnabled = false;
    Poseidon::TLMaterial _materialSet;
    int _materialSetSpec = 0;
    // cache key for the active local-light list.
    // changing lights forces a re-upload even when the material is unchanged.
    uint64_t _materialSetLightsSig = 0;
#ifndef NDEBUG
    // diagnostic verification signature for frame-constant lighting state.
    // checks that state folded into dosetmaterial matches the cached context.
    // catches caches that survive into the next frame with stale lighting data.
    uint64_t _materialFrameInputsSig = 0;
#endif

    RString _pendingScreenshotPath;

    void DrawDecal(Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color,
                   const Poseidon::MipInfo& mip, int specFlags) override;
    void DrawPolygon(const VertexIndex* ii, int n) override;
    void DrawSection(const FaceArray& face, Offset beg, Offset end) override;
    void DrawPoints(const TLVertex* vs, int nVertex);
    void DrawPoints(int beg, int end) override;
    bool CanGrass() const override;

    void Draw2D(const Poseidon::Draw2DPars& pars, const Poseidon::Rect2DAbs& rect,
                const Poseidon::Rect2DAbs& clip) override;
    void DrawPoly(const Poseidon::MipInfo& mip, const Poseidon::Vertex2DPixel* vertices, int nVertices,
                  const Poseidon::Rect2DPixel& clip, int specFlags) override;
    void DrawPoly(const Poseidon::MipInfo& mip, const Poseidon::Vertex2DAbs* vertices, int nVertices,
                  const Poseidon::Rect2DAbs& clip, int specFlags) override;
    void DrawLine(const Poseidon::Line2DAbs& line, PackedColor c0, PackedColor c1,
                  const Poseidon::Rect2DAbs& clip) override;
    void DrawLine(int beg, int end) override;

    void DoSetMaterial(const Poseidon::TLMaterial& mat, const LightList& lights,
                       const Poseidon::render::LegacySpec& spec);
    void SetMaterial(const Poseidon::TLMaterial& mat, const LightList& lights,
                     const Poseidon::render::LegacySpec& spec) override;

    void Screenshot(RString filename) override { _pendingScreenshotPath = static_cast<const char*>(filename); }
    void FlushPendingScreenshot() override { CaptureScreenshotIfPending(); }
    bool CanRestore() { return false; }

    void SwitchRenderMode(RenderMode mode)
    {
        if (_renderMode == mode)
            return;
        DoSwitchRenderMode(mode);
    }

    enum TexGenMode
    {
        TGFixed,
        TGNone,
        TGDetail,
        TGGrass,
        TGWater
    };

    enum class BlendMode
    {
        Opaque,
        AlphaBlend,
        Additive,
        Shadow
    };

    enum class DepthMode
    {
        Normal,
        ReadOnly,
        Disabled,
        Shadow
    };

  protected:
    int _pixelSize;
    int _depthBpp;
    int _refreshRate;
    Poseidon::WindowMode _windowMode = Poseidon::WindowMode::Borderless;
    int _windowedRestoreW = 0;
    int _windowedRestoreH = 0;
    bool _pendingExclusiveEnter = false;

    SDL_Window* _sdlWindow = nullptr;
    SDLEventWindow _eventWindow;

    int _bias;
    float _grassParam[4];
    bool _clipANearEnabled, _clipAFarEnabled;
    Plane _clipANear, _clipAFar;

    int _minGuardX;
    int _maxGuardX;
    int _minGuardY;
    int _maxGuardY;

    bool _windowed;

  protected:
    TextBankGLES32* _textBank = nullptr;

  protected:
    // sdl_glcontext handle.
    // stored as a void pointer so this interface does not need SDL headers.
    void* _glContext = nullptr;

    int _prepSpec;
    // caches the texture object currently bound to texture unit 1.
    // keeps recorded draw commands aligned with the handle when setmultitexturing exits early.
    unsigned int _lastTexture1Handle = 0;
    bool _stencilExclusionEnabled;
    TexGenMode _texGenMode;
    Poseidon::PassId _activePassId = Poseidon::PassId::ScreenSpace;

    int _iOffset;

    QueueGLES32* _lastQueueSource;

    // core pipeline objects for dynamic and queued rendering.
    unsigned int _vaoScreen = 0; // tlvertex layout bindings.
    unsigned int _vaoMesh = 0;   // svertex layout bindings.
    unsigned int _vbo = 0;
    unsigned int _ibo = 0;

    // 1x1 opaque white sentinel texture.
    // binds to active samplers when a draw omits a texture map.
    // sampler name 0 is undefined in glsl, so this keeps texture modulation a no-op against vertex colors.
    unsigned int _fallbackWhiteTex = 0;

  public:
    // dedicated texture unit for asynchronous uploads.
    // isolates data transfer work so units 0 and 1 keep the active render state.
    // prevents demand-loaded textures from stealing bindings between cached draw calls.
    static constexpr unsigned int kUploadUnit = 0x84C7; // gl_texture7.
  protected:
    bool _lastClampU, _lastClampV;
    bool _pointSampling;
    bool _enableReorder;

    // sampler objects mapped by bitfield: point mode (4) | clamp u (1) | clamp v (2).
    unsigned int _samplerObjects[8] = {};
    void CreateSamplerStates();
    void DestroySamplerStates();
    void ApplySamplerState();

    TexLoc _texLoc;

    // static capability guarantees.
    // evaluated at compile time so baseline feature checks disappear.
    static constexpr bool _can565 = true;
    static constexpr bool _can88 = false;
    static constexpr bool _can8888 = true;
#ifdef __ANDROID__
    // texture compression support mask.
    // queried at runtime on android because mali hardware often lacks native s3tc.
    int _dxtFormats = 0; 
#else
    int _dxtFormats = 0x3E;
#endif
    static constexpr bool _hasStencilBuffer = true;
    static constexpr bool _canDetailTex = true;
    static constexpr bool _canZBias = true;

    // hardware alpha-to-coverage state tracking.
    // tracks whether the swapchain actually provisioned multisample buffers.
    // caches the toggle state to minimize driver calls during draw emission.
    bool _msaaActive = false;
    bool _alphaToCoverageCfg = true;
    bool _a2cBound = false;
    bool _debugFlatColor = false;

    // supersample anti-aliasing resources.
    // scaling is active when the target framebuffer object is non-zero.
    // renders into a 4x multisampled high-resolution buffer before resolving into a single-sample downscale target.
    float _renderScale = 1.0f;
    float _pendingRenderScale = 1.0f;
    int _msaaSamples = 0;
    int _pendingMsaaSamples = 0;
    unsigned int _ssaaFbo = 0;
    unsigned int _ssaaColorRb = 0;
    unsigned int _ssaaDepthRb = 0;
    unsigned int _ssaaResolveFbo = 0;
    unsigned int _ssaaResolveRb = 0;
    int _ssaaW = 0;
    int _ssaaH = 0;

    bool SSAAActive() const { return _ssaaFbo != 0; }
    // evaluates the pixel dimensions of the active render surface.
    // determines viewport and scissor bounds for the current frame target.
    void RenderTargetSize(int& w, int& h) const;
    void ApplyPendingRenderScale();
    void DestroySSAATarget();
    // blits the supersampled render target down into the swapchain backbuffer.
    // is a no-op when supersampling is disabled.
    void ResolveSSAAToDefault();
    // establishes the primary draw target for the frame and restores canonical viewport bounds.
    void BindFrameRenderTarget();

    // shader program object registry.
    // maps vertex and pixel shader combinations to linked driver handles.
    unsigned int _shaderProgram[NVertexShaders][NPixelShaderSpecular][NPixelShaderModes][NPixelShaders];
    PixelShaderID _pixelShaderSel;
    PixelShaderMode _pixelShaderModeSel;
    PixelShaderSpecular _pixelShaderSpecularSel;
    PSConstants _psConstants;

    VertexShaderID _vertexShaderSel = VSNone;
    // canonical storage for environmental fog configuration.
    // mutating this struct in place keeps shader constants synchronized across state invalidations.
    FrameState _frameState;
    std::vector<DrawItem> _drawItems;
    DrawItem _currentDrawItem;

    float _nightEye;
    int _dbgMeshDrawCalls = 0;
    int _dbgMeshTotalIndices = 0;
    int _dbgAddVerticesCalls = 0;
    int _dbgTotalVertices = 0;
    int _dbgQueueFanCalls = 0;
    int _dbgTotalFanTris = 0;

    QueueGLES32 _queueNo;

    // deferred vertex buffer mirror.
    // accumulates cpu-side vertex writes and flushes them to the gpu buffer in large blocks before drawing.
    // reduces driver overhead from submitting thousands of individual primitives.
    std::vector<TLVertex> _vboMirror;
    int _vboUploadedVerts = 0;

    float _gamma;

    bool _frameOpen;

    Color _textColor;

    enum VFormatSet
    {
        SingleTex,
        DetailTex,
        SpecularTex,
        GrassTex
    };
    VFormatSet _formatSet;

    enum class PipelineVertexInput
    {
        ActivePass,
        Screen,
        Mesh
    };

    // applies the full rendering pipeline state from a descriptor struct.
    // dispatches state changes to the individual cache helpers.
    // establishes the graphics context configuration required by the next draw call.
    void ApplyPipeline(const Poseidon::render::RenderPassDescriptor& d);

  public:
    // diagnostic region markers for graphics profilers.
    // forwards to the driver debug extension when it is available.
    void BeginDebugGroup(const char* name) override;
    void EndDebugGroup() override;

    // queries the active viewport bounds directly from driver state.
    bool GetGLViewport(int outRect[4]) const override;

    // dispatches the active draw configuration to the graphics hardware.
    void EmitDraw(const Poseidon::render::frame::Draw& d) override;

    // --- Properties ---
    RString GetDebugName() const override;
    RString GetRendererName() const override;
    size_t GetDrawItemCount() const override { return _drawItems.size(); }
    const std::vector<DrawItem>* GetRecordedDraws() const override { return &_drawItems; }
    unsigned int GetDebugErrorCount() const override;
    std::string GetLastDebugMessage() const override;
    int SampleBackBufferNonBlack() override;
    bool SamplePixel(int x, int y, uint8_t* outRGB) override;
    int Width() const override { return _w; }
    int Height() const override { return _h; }
    int PixelSize() const override { return _pixelSize; }
    int RefreshRate() const override { return _refreshRate; }
    bool CanBeWindowed() const override { return true; }
    bool IsWindowed() const override { return _windowed; }
    bool IsResizable() const override
    {
        return _sdlWindow && (SDL_GetWindowFlags(_sdlWindow) & SDL_WINDOW_RESIZABLE) != 0;
    }

    int Width2D() const { return _w; }
    int Height2D() const { return _h; }

    int MinGuardX() const override { return _minGuardX; }
    int MaxGuardX() const override { return _maxGuardX; }
    int MinGuardY() const override { return _minGuardY; }
    int MaxGuardY() const override { return _maxGuardY; }

    int MinSatX() const override { return _minGuardX; }
    int MaxSatX() const override { return _maxGuardX; }
    int MinSatY() const override { return _minGuardY; }
    int MaxSatY() const override { return _maxGuardY; }

  protected:
    void WorkToBack();
    void BackToFront();
    void CaptureScreenshotIfPending();

  public:
    EngineGLES32(int width, int height, bool windowed, int bpp);
    ~EngineGLES32() override;

    bool InitDrawDone() override;
    bool IsAbleToDraw() override;
    bool IsAbleToDrawCheckOnly();
    void InitDraw(bool clear = false, PackedColor color = PackedColor(0)) override;
    void FinishDraw() override;
    void NextFrame() override;
    void DrawTestPattern(const char* name) override;

    void Pause() override;
    void Restore() override;

    void PreReset(bool hard);
    void PostReset();

    bool Reset();
    bool ResetHard();
    void ResetForRemount() override; // re-initializes gpu resources without tearing down the display surface.

    bool SwitchRes(int w, int h, int bpp) override;
    bool SwitchRefreshRate(int refresh) override;
    bool SetWindowMode(Poseidon::WindowMode mode) override;
    Poseidon::WindowMode GetCurrentWindowMode() const override;
    void OnWindowResized(int w, int h) override;
    void OnFullscreenChanged(bool windowed) override;

    void ListResolutions(FindArray<ResolutionInfo>& ret) override;
    void ListRefreshRates(FindArray<int>& ret) override;
    void ListMonitors(FindArray<MonitorInfo>& ret) override;
    int GetCurrentMonitor() const override;
    bool SwitchMonitor(int idx) override;
    bool GetDesktopDisplayMode(int& w, int& h, int& refresh) const override;
    bool GetCurrentDisplayMode(int& w, int& h, int& refresh) const override;
    bool GetRequestedFullscreenMode(int& w, int& h, int& refresh) const override;
    bool SetSwapInterval(int interval) override;
    int GetSwapInterval() const override;

    void DestroySurfaces();

    TexLoc GetTexLoc() const { return _texLoc; }

    int DXTSupport() const { return _dxtFormats; }
    bool CanDXT(int i) const { return (_dxtFormats & (1 << i)) != 0; }

    bool Can565() const { return _can565; }
    bool Can88() const { return _can88; }
    bool Can8888() const { return _can8888; }

    bool GetHWTL() const { return true; }

    void Clear(bool clearZ = true, bool clear = true, PackedColor color = PackedColor(0)) override;

    VertexBuffer* CreateVertexBuffer(const Shape& src, VBType type) override;
    int CompareBuffers(const Shape& s1, const Shape& s2) override;

    int FrameTime() const;
    int AFrameTime() const override { return FrameTime(); }

    void DoSetGamma();
    void SetGamma(float gamma) override;
    float GetGamma() const override { return _gamma; }

    // delegates os event processing to the embedded window handler.
    void HandleEvents() override { _eventWindow.HandleEvents(); }
    bool IsOpen() const override { return _eventWindow.IsOpen(); }
    void SetMouseGrab(bool grab) override { _eventWindow.SetMouseGrab(grab); }
    bool IsMouseGrabbed() const override { return _eventWindow.IsMouseGrabbed(); }

    void EnableReorderQueues(bool enableReorder) override;
    void FlushQueues() override;

    void BeginShadowPass() override;
    void EndShadowPass() override;

    bool ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res,
                          float* outDepth) override;

    void SetShadowMapsEnabled(bool enabled) override { _shadowTuning.enabled = enabled; }
    bool ShadowMapsEnabled() const override { return _shadowTuning.enabled; }
    ShadowMapTuning GetShadowMapTuning() const override { return _shadowTuning; }
    void SetShadowMapTuning(const ShadowMapTuning& tuning) override { _shadowTuning = tuning; }
    void SetShadowMapSunFactor(float f) override { _shadowSunFactor = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }
    static constexpr int kShadowCascades = 4;
    void RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3,
                                int numCascades, int omniCount, int res, const ShadowCasterSet& casters) override;
    bool DumpShadowMap(const char* path) override;
    bool ShadowMapCacheSelfTest() override;
    // establishes texture and uniform bindings for shadow-map evaluation.
    // populates cascade matrices and depth maps so lit passes can calculate occlusion.
    // is a no-op when no shadow geometry was processed.
    void UpdateShadowMapLitState();

    ShadowMapTuning _shadowTuning;                 // runtime configuration block.
    float _shadowSunFactor = 1.0f;                 // modulates shadow intensity based on time of day.
    bool _shadowMapActive = false;                 // tracks whether depth evaluation is required this frame.
    unsigned int _shadowMapTex = 0;                // depth texture array object.
    int _shadowMapRes = 0;                         // shadow buffer resolution.
    int _shadowCascades = 0;                       // active count of split cascades.
    int _shadowOmniCount = 0;                      // cascade threshold for omni-directional spheres.
    float _shadowMapVP[kShadowCascades * 16] = {}; // column-major view-projection matrices per cascade.
    float _shadowSplits[kShadowCascades] = {};     // linear distance thresholds for cascade selection.
    float _shadowCamFwd[3] = {};                   // view-space forward vector for cascade evaluation.

    void GetZCoefs(float& zAdd, float& zMult) override;
    void SetBias(int bias) override;
    int GetBias() override { return _bias; }

    void SetGrassParams(float a1, float a2, float a3 = 0, float a4 = 0) override;

    bool CanZBias() const override;
    bool ZBiasExclusion() const override { return !_hasStencilBuffer; }

    AbstractTextBank* TextBank() override;
    TextBankGLES32* TextBankDD() const { return _textBank; }

    void CreateTextBank();
    void ReportGRAM(const char* name);

  protected:
    void DoSetGrassParamsPS();
    void UploadPSConstant(int reg, const float* data);
    void UploadPSFogColor(const Color& fogColor);
    void FlushVSConstants();
    void FlushPSConstants();

    BlendMode _currentBlendMode = BlendMode::Opaque;
    DepthMode _currentDepthMode = DepthMode::Normal;

    void StencilExclusion(bool enable)
    {
        if (enable == _stencilExclusionEnabled)
            return;
        DoStencilExclusion(enable, true);
    }
    void DoStencilExclusion(bool enable, bool optimize);

    void ChangeClipPlanes();
    void PrepareDetailTex(bool water, bool grass);
    void PrepareSingleTexModulateA();
    void PrepareSingleTexDiffuseA();
    void SetTexture(const TextureGLES32* tex, const Poseidon::render::LegacySpec& spec);
    void SetMultiTexturing(VFormatSet format);

    void EnableDetailTexGen(TexGenMode mode, bool optimize)
    {
        if (_texGenMode != mode)
            DoEnableDetailTexGen(mode, optimize);
    }
    void DoEnableDetailTexGen(TexGenMode mode, bool optimize);

    WORD* QueueAdd(QueueGLES32& queue, int n);
    void QueueFan(const VertexIndex* ii, int n);
    void Queue2DPoly(const TLVertex* v, int n);

    void FlushQueue(QueueGLES32& queue, int index);
    void FlushAndFreeQueue(QueueGLES32& queue, int index);

    int AllocateQueue(QueueGLES32& queue, TextureGLES32* tex, int level, int spec);
    void FreeQueue(QueueGLES32& queue, int index);
    void FreeAllQueues(QueueGLES32& queue);
    void FlushAndFreeAllQueues(QueueGLES32& queue, bool nonEmptyOnly = false);
    void FlushAllQueues(QueueGLES32& queue, int skip = -1);

    void CloseAllQueues(QueueGLES32& queue);

    void DoSwitchRenderMode(RenderMode mode);

    void D3DPreparePoint();
    void D3DPrepare3DLine();

    void ApplyPassState(TextureGLES32* tex, int level, const Poseidon::render::LegacySpec& spec, Poseidon::PassId passId,
                        PipelineVertexInput vertexInput);

    // filters redundant state transitions.
    // skips pipeline rebinding when the incoming pass descriptor matches the existing context.
    // manual driver state changes must explicitly invalidate this cache block.
    struct
    {
        Poseidon::render::RenderPassDescriptor d;
        bool in3d = false;
        bool a2c = false;
        PipelineVertexInput vertexInput = PipelineVertexInput::ActivePass;
        bool valid = false;
    } _lastApplied;
    PipelineVertexInput _pipelineVertexInput = PipelineVertexInput::ActivePass;
    void InvalidatePipelineCache() { _lastApplied.valid = false; }

    // hardware instancing state tracker.
    // groups identical mesh draws into batched instanced calls driven by a transform buffer.
    // dynamic geometry emission within a run forces a fallback to scalar dispatch.
    void BeginInstancedRun(int count)
    {
        _instCount = count;
        _instImpure = false;
    }
    bool EndInstancedRun() override
    {
        const bool pure = !_instImpure;
        _instCount = 0;
        return pure;
    }
    void UploadWorldInstances(const float* matrices, int count);
    // collects and converts model-to-world transforms into camera-relative matrices for uniform upload.
    void InstancedRunReset() override { _instPending = 0; }
    bool InstancedRunAdd(const Matrix4& modelToWorld) override;
    int InstancedRunPending() const { return _instPending; }
    void BeginInstancedRunUpload() override;
    int _instCount = 0;
    bool _instImpure = false;
    int _instPending = 0;
    GfxMatrix _instArray[256];
    void QueuePrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags);

    void PrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags) override;
    void PrepareTriangleTL(const Poseidon::MipInfo& mip, const Poseidon::render::LegacySpec& spec) override;

    bool GetTL() const override { return true; }
    bool GetTLOnSurface() const override { return true; }

    // toggles hardware alpha-to-coverage evaluation when multisampling is provisioned.
    void SetAlphaToCoverage(bool enable) override { _alphaToCoverageCfg = enable; }
    bool GetAlphaToCoverage() const override { return _alphaToCoverageCfg && _msaaActive; }

    // diagnostic flat shading mode.
    // forces objects to render solid red while preserving alpha testing and clipping.
    // highlights geometry borders separately from texture data.
    void SetDebugFlatColor(bool enable) override
    {
        _debugFlatColor = enable;
        InvalidatePipelineCache();
    }
    bool GetDebugFlatColor() const override { return _debugFlatColor; }

    // sets the supersampling resolution scale factor.
    // changes take effect on the next frame swap so target dimensions stay coherent during active rendering.
    void SetRenderScale(float scale) override;
    float GetRenderScale() const override { return _renderScale; }
    void SetMsaaSamples(int samples) override;
    int GetMsaaSamples() const override { return _msaaSamples; }
    bool HasWBuffer() const override { return false; }

    bool IsWBuffer() const override { return false; }
    bool CanWBuffer() const override { return false; }
    void SetWBuffer(bool) override {}

    void BeginPass(Poseidon::PassId passId);
    void BeginScreenPass();

    // groups render passes logically for external graphics debuggers.
    void SwitchPassDebugGroup(const char* name);
    void ClosePassDebugGroup();
    bool _passDebugGroupOpen = false;

    void DiscardVB();
    void AddVertices(const TLVertex* v, int n);
    void UploadPendingVertices(); // commits the deferred cpu vertex mirror to driver memory.

  public:
    bool IsIn3DPass() const { return _activePassId != Poseidon::PassId::ScreenSpace; }
    void EnableSunLight(bool enable) override;

    int AddLight(Light* light);
    void ClearLights();

    void UpdateProjection() override;

    // enforces aspect-ratio boundaries via viewport cropping.
    // restricts the 3d projection to a sub-rectangle and clears the margins.
    void ApplyWorldViewport();
    void EndWorldViewport();
    bool _worldViewportActive = false;
    void PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld,
                       const Poseidon::render::LegacySpec& spec) override;
    void PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld,
                           const Poseidon::render::LegacySpec& spec);
    void BeginMeshTL(const Shape& sMesh, int spec, bool dynamic = false) override;
    void EndMeshTL(const Shape& sMesh) override;
    void DrawSectionTL(const Shape& sMesh, int beg, int end) override;

    void InitGL();
    void ShutdownGL();
    void TextureDestroyed(Texture* tex) override;

  public:
    void CreateVB();
    void DestroyVB();

    void CreateVBTL();
    void DestroyVBTL();

    void RestoreVB();

    void FogColorChanged(ColorVal fogColor) override { SetFogColor(fogColor); }
    void EnableNightEye(float night) override;

    void PrepareMesh(const Poseidon::render::LegacySpec& spec) override;
    void BeginMesh(TLVertexTable& mesh, const Poseidon::render::LegacySpec& spec) override;
    void EndMesh(TLVertexTable& mesh) override;

    void InitPixelShaders();
    void DeinitPixelShaders();

    void SelectPixelShaderMode(PixelShaderMode mode)
    {
        if (_pixelShaderModeSel == mode)
            return;
        DoSelectPixelShader(_pixelShaderSel, mode, _pixelShaderSpecularSel);
    }
    void SelectPixelShaderSpecular(PixelShaderSpecular spec)
    {
        if (_pixelShaderSpecularSel == spec)
            return;
        DoSelectPixelShader(_pixelShaderSel, _pixelShaderModeSel, spec);
    }

    void SelectPixelShader(PixelShaderID ps)
    {
        if (_pixelShaderSel == ps)
            return;
        DoSelectPixelShader(ps, _pixelShaderModeSel, _pixelShaderSpecularSel);
    }
    void DoSelectPixelShader(PixelShaderID ps, PixelShaderMode mode, PixelShaderSpecular spec);

    void InitVertexShaders();
    void DeinitVertexShaders();
    void SelectVertexShader(VertexShaderID vs);
    void UploadVSScreenConstants();
    void UploadVSProjection(const FrameState& frame);
    void UploadVSViewConstants(const FrameState& frame);
    FrameState BuildFrameState(Camera* camera, LightSun* sun, int bias, const Color& fogColor, bool sunEnabled);
    PassState BuildPassState(const FrameState& frame, Poseidon::PassId passId);
    void UploadVSWorldMatrix(const float worldMatrix[16]);
    void UploadVSMaterialConstants(const Poseidon::TLMaterial& mat, bool sunEnabled);
    void UploadVSLights(const LightList& lights, const Poseidon::TLMaterial& mat, float nightEffect);
    void UploadVSTexGenConstants(TexGenMode mode);
    void SetShaderFogEnabled(bool enabled);

    void UploadFrameConstants(const FrameState& frame);
    void UploadPassConstants(const PassState& pass);
    void UploadObjectConstants(const DrawItem& item);

    void ApplyBlendMode(BlendMode mode);
    void ApplyDepthMode(DepthMode mode);
    void SetAlphaTest(bool enable, DWORD ref = 0xc0, bool alphaToCoverage = false);

    void Init3DState();
    void Init3D();
    void SetFogColor(ColorVal fog);

    float ZShadowEpsilon() const override { return 0.01f; }
    float ZRoadEpsilon() const override { return 0.005f; }

    float ObjMipmapCoef() const override { return 1.5f; }
    float LandMipmapCoef() const { return 1.0f; }

  private:
    // raii teardown sequence guard.
    // declared at the bottom of the class so it is destroyed first during class destruction.
    // clears the base class font cache before the backend text bank is uninitialized.
    // prevents dangling texture references from being released into a destroyed context.
    struct ShutdownGuard
    {
        EngineGLES32* engine;
        ~ShutdownGuard();
    };
    ShutdownGuard _shutdownGuard{this};
};

#endif
