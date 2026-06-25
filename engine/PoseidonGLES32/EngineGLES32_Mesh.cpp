#include <PoseidonGLES32/EngineGLES32.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

void EngineGLES32::PrepareMesh(const Poseidon::render::LegacySpec& /*spec*/)
{
    BeginScreenPass();
    ChangeClipPlanes();
}

void EngineGLES32::BeginMesh(TLVertexTable& mesh, const Poseidon::render::LegacySpec& /*spec*/)
{
    BeginScreenPass();
    _mesh = &mesh;

    AddVertices(mesh.VertexData(), mesh.NVertex());
}

void EngineGLES32::EndMesh(TLVertexTable& mesh)
{
    _mesh = nullptr;
}

void EngineGLES32::UpdateProjection()
{
    if (IsIn3DPass())
    {
        // Flush is the load-bearing step: pending draws must commit with the old
        // projection before the change. (_drawItems is the per-frame draw recording
        // the flush appends to — not a pending-draw count — so it is legitimately
        // non-empty mid-pass.)
        FlushAndFreeAllQueues(_queueNo, true);
        Camera* camera = GScene->GetCamera();
        int projBias = _canZBias ? 0 : _bias;
        ConvertProjectionMatrix(_frameState.projection, camera->ProjectionNormal(), projBias);
        UploadVSProjection(_frameState);
    }
}

bool EngineGLES32::InstancedRunAdd(const Matrix4& modelToWorld)
{
    if (_instPending >= 256)
        return false;
    GfxMatrix& m = _instArray[_instPending];
    ConvertMatrix(m, modelToWorld);
    m._41 -= _frameState.cameraPos[0];
    m._42 -= _frameState.cameraPos[1];
    m._43 -= _frameState.cameraPos[2];
    ++_instPending;
    return true;
}

void EngineGLES32::BeginInstancedRunUpload()
{
    UploadWorldInstances(reinterpret_cast<const float*>(_instArray), _instPending);
    BeginInstancedRun(_instPending);
}

void EngineGLES32::PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    FlushAndFreeAllQueues(_queueNo, true);
    BeginPass(SpecToPassId(spec));
    PrepareMeshTLImpl(_frameState, modelToWorld, spec);
}

void EngineGLES32::PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    EnableSunLight(!Poseidon::render::Has(spec.material, Poseidon::render::Material::DisableSun));
    ChangeClipPlanes();

    GfxMatrix worldMatrix;
    ConvertMatrix(worldMatrix, modelToWorld);
    // Camera-relative rendering
    worldMatrix._41 -= frame.cameraPos[0];
    worldMatrix._42 -= frame.cameraPos[1];
    worldMatrix._43 -= frame.cameraPos[2];

    _currentDrawItem = DrawItem{};
    _currentDrawItem.worldMatrix = worldMatrix;
    _currentDrawItem.specFlags = spec;
    _currentDrawItem.bias = _bias;

    UploadObjectConstants(_currentDrawItem);

    // IsColored objects carry their opacity + fade in the scene constant colour;
    // mirror the software path (TransLight.cpp) or they render at texture alpha.
    float constColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (GScene && Poseidon::render::Has(spec.routing, Poseidon::render::Routing::IsColored))
    {
        ColorVal cc = GScene->GetConstantColor();
        constColor[0] = cc.R();
        constColor[1] = cc.G();
        constColor[2] = cc.B();
        constColor[3] = cc.A();
    }
    if (memcmp(constColor, _psConstants.constColor, sizeof(constColor)) != 0)
    {
        memcpy(_psConstants.constColor, constColor, sizeof(constColor));
        UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);
    }
}

void EngineGLES32::BeginMeshTL(const Shape& sMesh, int spec, bool dynamic)
{
    sMesh.GetVertexBuffer()->Update(sMesh, dynamic);
}

void EngineGLES32::EndMeshTL(const Shape& sMesh)
{
    ClearLights();
}
