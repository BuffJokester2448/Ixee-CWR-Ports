#include <PoseidonGLES32/EngineGLES32.hpp>
#include <PoseidonGLES32/GLES32BindCache.hpp>
#include <PoseidonGLES32/TextureGLES32.hpp>
#include <Poseidon/Graphics/Core/FanDecompose.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <Poseidon/Graphics/Core/GLPipelineState.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <PoseidonGLES32/GLESCompat.hpp>
#include <cstdio>

WORD* EngineGLES32::QueueAdd(QueueGLES32& queue, int n)
{
    if (_instCount > 1)
        _instImpure = true; // soup-queue geometry cannot be instanced, so the run must fall back.

    PoseidonAssert(queue._actTri >= 0);
    PoseidonAssert(queue._triUsed[queue._actTri]);
    TriQueue& triq = queue._tri[queue._actTri];
    if (triq._triangleQueue.Size() + n > TriQueueSize)
        FlushQueue(queue, queue._actTri);

    int index = triq._triangleQueue.Size();
    triq._triangleQueue.Resize(index + n);
    return triq._triangleQueue.Data() + index;
}

void EngineGLES32::QueueFan(const VertexIndex* ii, int n)
{
    const int addN = Poseidon::render::geom::FanTriangleIndexCount(n);
    if (addN == 0)
        return;

    WORD* tgt = QueueAdd(_queueNo, addN);
    if (!tgt || !ii)
        return;

    _dbgQueueFanCalls++;
    _dbgTotalFanTris += addN;

    const int offset = _queueNo._meshBase;
    PoseidonAssert(offset >= 0);

    Poseidon::render::geom::FanToTriangles(ii, n, offset, tgt);
}

void EngineGLES32::Queue2DPoly(const TLVertex* v0, int n)
{
    int addN = (n - 2) * 3;
    PoseidonAssert(_queueNo._actTri >= 0);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
    WORD* tgt = QueueAdd(_queueNo, addN);

    int offset = _queueNo._meshBase;
    PoseidonAssert(offset >= 0);

    for (int i = 2; i < n; i++)
    {
        *tgt++ = 0 + offset;
        *tgt++ = i - 1 + offset;
        *tgt++ = i + offset;
    }
}

void EngineGLES32::FlushQueue(QueueGLES32& queue, int index)
{
    TriQueue& triq = queue._tri[index];
    int n = triq._triangleQueue.Size();
    if (n > 0)
    {
        // upload the deferred vertex range before any draw consumes it.
        UploadPendingVertices();

        if (index == MaxTriQueues - 1)
            FlushAllQueues(queue, index);

        ApplyPassState(triq._texture, triq._level, Poseidon::render::SplitLegacy(triq._special), triq._passId,
                       PipelineVertexInput::Screen);

        if (!_vaoScreen || !_vbo || !_ibo)
        {
            triq._triangleQueue.Clear();
            return;
        }

        // SelectVertexShader only rebinds the vao when the shader changes, so a
        // same-shader call can leave a different vao bound from another path.
        // bind _vaoScreen explicitly because it matches the TLVertex layout.
        GLES32Bind::Vao(_vaoScreen);
        // this direct vao bind desynchronizes the vao from _vertexShaderSel, which
        // ApplyPipeline assumes stays paired with the shader selection.
        // invalidate the pipeline cache so the next 3d draw reselects its shader
        // and rebinds the mesh vao.
        InvalidatePipelineCache();

        // upload indices.
        int indexOffset = 0;
        int ibSize = n * sizeof(WORD);
        Poseidon::render::ibo::BindOnActiveVao(_ibo);

        if (n + queue._indexBufferUsed <= IndexBufferLength && !queue._firstIndex)
        {
            indexOffset = queue._indexBufferUsed;
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, indexOffset * sizeof(WORD), ibSize, triq._triangleQueue.Data());
        }
        else
        {
            queue._firstIndex = false;
            indexOffset = 0;
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, IndexBufferLength * sizeof(WORD), nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibSize, triq._triangleQueue.Data());
        }
        queue._indexBufferUsed = indexOffset + n;

        // bind the vertex buffer.
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);

        // draw.
        FlushVSConstants();
        FlushPSConstants();

        glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_SHORT, (void*)(intptr_t)(indexOffset * sizeof(WORD)));
        ++Poseidon::gPerfDrawCalls;

        // record the draw item.
        DrawItem item = {};
        item.isTLDraw = false;
        item.specFlags = Poseidon::render::SplitLegacy(triq._special);
        item.passId = triq._passId;
        _drawItems.push_back(item);

        triq._triangleQueue.Clear();
    }
}

void EngineGLES32::FlushAndFreeQueue(QueueGLES32& queue, int index)
{
    FlushQueue(queue, index);
    FreeQueue(queue, index);
}

int EngineGLES32::AllocateQueue(QueueGLES32& queue, TextureGLES32* tex, int level, int spec)
{
    bool alpha = (tex != nullptr && tex->IsAlpha()) || !_enableReorder;
    int minI = 0;
    int maxI = MaxTriQueues - 1;
    if (alpha)
    {
        minI = MaxTriQueues - 1;
        maxI = MaxTriQueues;
        FlushAllQueues(queue, MaxTriQueues - 1);
    }

    int index = queue.Allocate(tex, level, spec, minI, maxI, queue._actTri);
    if (index >= 0)
    {
        PoseidonAssert(queue._triUsed[index]);
        return index;
    }
    // free the least-recently-used queue.
    int minUsed = INT_MAX;
    for (int i = minI; i < maxI; i++)
    {
        int used = queue._tri[i]._lastUsed;
        if (used < minUsed)
        {
            minUsed = used;
            index = i;
        }
    }
    if (index < 0)
        index = 0;
    FlushAndFreeQueue(queue, index);
    index = queue.Allocate(tex, level, spec, minI, maxI, index);
    PoseidonAssert(index >= 0);
    PoseidonAssert(queue._triUsed[index]);
    return index;
}

void EngineGLES32::FreeQueue(QueueGLES32& queue, int index)
{
    PoseidonAssert(!queue._tri[index]._triangleQueue.Size());
    queue.Free(index);
}

void EngineGLES32::FreeAllQueues(QueueGLES32& queue)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (queue._triUsed[i])
        {
            queue._tri[i]._triangleQueue.Clear();
            FreeQueue(queue, i);
        }
    }
}

void EngineGLES32::FlushAndFreeAllQueues(QueueGLES32& queue, bool nonEmptyOnly)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (queue._triUsed[i] && (!nonEmptyOnly || queue._tri[i]._triangleQueue.Size() > 0))
            FlushAndFreeQueue(queue, i);
    }
}

void EngineGLES32::FlushAllQueues(QueueGLES32& queue, int skip)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (i != skip && queue._triUsed[i])
            FlushQueue(queue, i);
    }
}

void EngineGLES32::CloseAllQueues(QueueGLES32& queue)
{
    FlushAndFreeAllQueues(queue);
    queue._usedCounter = 0;
    queue._firstVertex = true;
}

void EngineGLES32::DoSwitchRenderMode(RenderMode mode)
{
    FlushAndFreeAllQueues(_queueNo);
    _renderMode = mode;
}

void EngineGLES32::D3DPreparePoint() {}
void EngineGLES32::D3DPrepare3DLine() {}

void EngineGLES32::ApplyPassState(TextureGLES32* tex, int level, const Poseidon::render::LegacySpec& spec, PassId passId,
                                PipelineVertexInput vertexInput)
{
    // state derivation lives in BuildRenderPassDescriptor; ApplyPassState only
    // assembles the build context, translates it, and binds the result.
    // the descriptor is the single seam that decodes spec bits.
    Poseidon::render::BuildContext ctx;
    ctx.isIn3DPass = vertexInput == PipelineVertexInput::Mesh
                   ? true
                   : vertexInput == PipelineVertexInput::Screen ? false : IsIn3DPass();
    ctx.isMultitexturing = IsMultitexturing();
    ctx.shadowAlphaRef = static_cast<std::uint8_t>((_shadowFactor * 7) >> 4);
    ctx.passKindHint = GetPassKindHint();

    const Poseidon::render::RenderPassDescriptor d = Poseidon::render::BuildRenderPassDescriptor(spec, ctx);
    const PipelineVertexInput previousVertexInput = _pipelineVertexInput;
    _pipelineVertexInput = vertexInput;
    ApplyPipeline(d);
    _pipelineVertexInput = previousVertexInput;

    // IsTexBound is the only gate here because it reflects OnTexDeleted and
    // keeps recycled handles from being skipped silently.
    unsigned int tHandle = tex ? tex->GetHandle() : 0;
    if (!GLES32Bind::IsTexBound(0, tHandle))
    {
        SetTexture(tex, spec);
    }
}

void EngineGLES32::QueuePrepareTriangle(const MipInfo& absMip, int specFlags)
{
    TextureGLES32* tex = reinterpret_cast<TextureGLES32*>(absMip._texture);
    int level = absMip._level;
    _queueNo._actTri = AllocateQueue(_queueNo, tex, level, specFlags);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
}

void EngineGLES32::PrepareTriangle(const MipInfo& absMip, int specFlags0)
{
    TextureGLES32* tex = reinterpret_cast<TextureGLES32*>(absMip._texture);
    SwitchRenderMode(RMTris);
    BeginScreenPass();
    int level = absMip._level;
    _queueNo._actTri = AllocateQueue(_queueNo, tex, level, specFlags0);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
    _prepSpec = specFlags0;
    LOG_DEBUG(Graphics, "GLES32: PrepareTriangle spec=0x{:x} tex={} level={} qIdx={} meshBase={}", specFlags0,
              tex ? tex->GetHandle() : 0, level, _queueNo._actTri, _queueNo._meshBase);
}

void EngineGLES32::PrepareTriangleTL(const MipInfo& mip, const Poseidon::render::LegacySpec& spec)
{
    PoseidonAssert(IsIn3DPass());
    TextureGLES32* tex = reinterpret_cast<TextureGLES32*>(mip._texture);
    int level = mip._level;
    PassId passId = SpecToPassId(spec);
    LOG_DEBUG(Graphics, "GLES32: PrepareTriangleTL spec=0x{:x} tex={} level={} passId={} vs={} in3D={}",
              Poseidon::render::MergeLegacy(spec), tex ? tex->GetHandle() : 0, level, static_cast<int>(passId),
              static_cast<int>(_vertexShaderSel), IsIn3DPass());
    ApplyPassState(tex, level, spec, passId, PipelineVertexInput::Mesh);
}

void EngineGLES32::BeginPass(PassId passId)
{
    if (IsIn3DPass())
    {
        LOG_DEBUG(Graphics, "GLES32: BeginPass({}) already in 3D (passId={}), just updating", static_cast<int>(passId),
                  static_cast<int>(_activePassId));
        if (_activePassId != passId)
            SwitchPassDebugGroup(PassIdName(passId));
        _activePassId = passId;
        return;
    }
    LOG_DEBUG(Graphics, "GLES32: BeginPass({}) from ScreenSpace — FULL INIT", static_cast<int>(passId));
    FlushAndFreeAllQueues(_queueNo);
    SwitchPassDebugGroup(PassIdName(passId));
    _activePassId = passId;

    SelectVertexShader(VSTransform);
    // BeginPass boots the 3d pass through the normal mesh shader before the
    // first descriptor-owned draw.
    // if the previous 3d draw had the same descriptor as the first draw in this
    // pass, ApplyPipeline could otherwise skip and leave VSTransform paired
    // with PSShadow.
    InvalidatePipelineCache();

    // d3d convention treats clockwise as front face. with glClipControl(GL_LOWER_LEFT),
    // no viewport y-flip occurs, so mesh winding is preserved from ndc to window.
    Poseidon::render::cull::Back();
    Poseidon::render::cull::FrontFaceCW();
    Poseidon::render::pipeline::EnableDepthTest();
    Poseidon::render::pipeline::DisableDepthClamp();
    // colour writes stay rgba for the whole 3d pass.
    // ApplyPipeline no longer toggles the color mask per draw, so assert it
    // once here and keep the invariant explicit.
    Poseidon::render::pipeline::SetColorMask(true);

    if (GScene)
    {
        _frameState = BuildFrameState(GScene->GetCamera(), GScene->MainLight(), _bias, _fogColor, _sunEnabled);
        _drawItems.clear();
        _currentDrawItem = DrawItem{};

        UploadFrameConstants(_frameState);
        // bind the previous frame's shadow depth map and light-vp for the lit
        // shaders. this is a no-op until a depth pass has run with shadow maps
        // enabled.
        UpdateShadowMapLitState();
    }

    // crop the 3d scene to the AspectSettings world rect.
    // this is a no-op when the rect is already full.
    ApplyWorldViewport();
}

void EngineGLES32::BeginScreenPass()
{
    if (!IsIn3DPass())
        return;
    LOG_DEBUG(Graphics, "GLES32: BeginScreenPass (was passId={})", static_cast<int>(_activePassId));
    FlushAndFreeAllQueues(_queueNo);
    // restore the full-window viewport and black-fill the cropped periphery
    // before any 2d or hud draws.
    // this is a no-op when the world was not cropped this frame.
    EndWorldViewport();
    SwitchPassDebugGroup(PassIdName(PassId::ScreenSpace));
    _activePassId = PassId::ScreenSpace;

    // reset the IsColored tint so a leftover mesh value cannot dim the HUD.
    _psConstants.constColor[0] = 1.0f;
    _psConstants.constColor[1] = 1.0f;
    _psConstants.constColor[2] = 1.0f;
    _psConstants.constColor[3] = 1.0f;
    UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);

    SelectVertexShader(VSScreen);
    UploadVSScreenConstants();

    // keep glEnable(GL_CULL_FACE) from BeginPass.
    // disabling it here lets the M113 wreck's coplanar wheel decal sections
    // fight at the same z and produce a cross-hatch artifact.
    // with culling enabled, the gpu drops one face of each pair and the wheels
    // stay clean.
    Poseidon::render::pipeline::EnableDepthClamp();
}

void EngineGLES32::DiscardVB() {}

void EngineGLES32::AddVertices(const TLVertex* v, int n)
{
    if (n <= 0)
        return;
    if (!_vbo)
        return;
    if (n > MeshBufferLength)
    {
        LOG_ERROR(Graphics, "Needed {} vertices, {} available", n, static_cast<int>(MeshBufferLength));
        return;
    }

    _dbgAddVerticesCalls++;
    _dbgTotalVertices += n;

    if (static_cast<int>(_vboMirror.size()) < MeshBufferLength)
        _vboMirror.resize(MeshBufferLength);

    // append to the cpu mirror only; the gl upload of the accumulated range is
    // deferred to UploadPendingVertices and performed before the consuming draw.
    // batching the upload removes the per-primitive driver round trip that
    // dominated map and 2d draw cost.
    if (_queueNo._vertexBufferUsed + n <= MeshBufferLength && !_queueNo._firstVertex)
    {
        memcpy(&_vboMirror[_queueNo._vertexBufferUsed], v, sizeof(TLVertex) * n);
        _queueNo._meshBase = _queueNo._vertexBufferUsed;
        _queueNo._meshSize = n;
        _queueNo._vertexBufferUsed += n;
    }
    else
    {
        _queueNo._firstVertex = false;
        // flush the pending mirror range and queued triangles before orphaning,
        // so nothing references the buffer we are about to discard.
        FlushAndFreeAllQueues(_queueNo);
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferData(GL_ARRAY_BUFFER, MeshBufferLength * sizeof(TLVertex), nullptr, GL_DYNAMIC_DRAW);
        _vboUploadedVerts = 0; // buffer discarded; mirror[0..) is pending again.
        memcpy(&_vboMirror[0], v, sizeof(TLVertex) * n);
        _queueNo._meshBase = 0;
        _queueNo._meshSize = n;
        _queueNo._vertexBufferUsed = n;
    }
}

void EngineGLES32::UploadPendingVertices()
{
    if (!_vbo)
        return;
    const int used = _queueNo._vertexBufferUsed;
    if (_vboUploadedVerts >= used)
        return;
    const int first = _vboUploadedVerts;
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferSubData(GL_ARRAY_BUFFER, first * sizeof(TLVertex), (used - first) * sizeof(TLVertex), &_vboMirror[first]);
    _vboUploadedVerts = used;
}

void EngineGLES32::EnableReorderQueues(bool enableReorder)
{
    if (_enableReorder == enableReorder)
    {
        return;
    }
    _enableReorder = enableReorder;
    if (!_enableReorder)
    {
        FlushQueues();
    }
}

void EngineGLES32::FlushQueues()
{
    FlushAndFreeAllQueues(_queueNo);
}
