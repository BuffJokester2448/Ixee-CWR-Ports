#include <PoseidonGLES32/EngineGLES32.hpp>
#include <PoseidonGLES32/GLES32BindCache.hpp>
#include <PoseidonGLES32/TextureGLES32.hpp>

#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <PoseidonGLES32/GLVertexAttribLayouts.hpp>

#include <PoseidonGLES32/GLESCompat.hpp>

void EngineGLES32::CreateTextBank()
{
    _textBank = new TextBankGLES32(this);
}

void EngineGLES32::CreateVB()
{
    if (!_glContext)
        return;

    // Core profile requires a non-zero VAO bound for any
    // GL_ELEMENT_ARRAY_BUFFER bind (IBO binding is part of VAO state).
    // Gen everything up front, then bind each VAO and configure the
    // shared VBO/IBO inside it.
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ibo);
    glGenVertexArrays(1, &_vaoScreen);
    glGenVertexArrays(1, &_vaoMesh);

    size_t stride = sizeof(TLVertex);

    // --- VAO for screen-space rendering (vsScreen) ---
    // TLVertex layout: pos(vec3), rhw(float), color(BGRA), specular(BGRA), uv0(vec2), uv1(vec2)
    GLES32Bind::Vao(_vaoScreen);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, MeshBufferLength * sizeof(TLVertex), nullptr, GL_DYNAMIC_DRAW);
    Poseidon::render::ibo::BindOnActiveVao(_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, IndexBufferLength * sizeof(WORD), nullptr, GL_DYNAMIC_DRAW);

    Poseidon::render::vao::SetupTLVertexLayout();

    // --- VAO for 3D mesh rendering (vsTransform) ---
    // Reads TLVertex data but interprets as: SVertex (pos, norm, uv).
    // Normal reads (rhw + color + specular) as 3 floats — same junk as D3D11.
    GLES32Bind::Vao(_vaoMesh);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    Poseidon::render::ibo::BindOnActiveVao(_ibo);

    Poseidon::render::vao::SetupSVertexLayout();

    GLES32Bind::Vao(0);

    _queueNo._vertexBufferUsed = 0;
    _queueNo._indexBufferUsed = 0;
}

void EngineGLES32::DestroyVB()
{
    if (_ibo)
    {
        glDeleteBuffers(1, &_ibo);
        _ibo = 0;
    }
    if (_vbo)
    {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }
    if (_vaoScreen)
    {
        GLES32Bind::OnVaoDeleted(_vaoScreen);
        glDeleteVertexArrays(1, &_vaoScreen);
        _vaoScreen = 0;
    }
    if (_vaoMesh)
    {
        GLES32Bind::OnVaoDeleted(_vaoMesh);
        glDeleteVertexArrays(1, &_vaoMesh);
        _vaoMesh = 0;
    }
    _lastQueueSource = nullptr;
}

void EngineGLES32::CreateVBTL() {}
void EngineGLES32::DestroyVBTL() {}

void EngineGLES32::RestoreVB() {}
