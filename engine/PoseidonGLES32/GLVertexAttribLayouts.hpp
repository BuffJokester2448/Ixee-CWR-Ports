#pragma once

#include <PoseidonGLES32/GLESCompat.hpp>

#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <PoseidonGLES32/EngineGLES32.hpp> // SVertex

#include <cstddef>

// vertex attribute layouts for the two vertex formats used by the gles32
// backend.
//
// tlvertex is the screen-space, pre-transformed layout consumed by
// vsScreen. svertex is the 3d mesh layout consumed by vstTransform. both
// layouts share the same physical vertex buffer; the active vao decides how
// the gpu interprets the bytes.
//
// when tlvertex-shaped data is read through the svertex layout, the normal
// attribute sees the rhw, color, and specular bytes instead of a true normal.
// that result is expected and matches the d3d11 backend.
//
// keeping the layouts here centralises the offsetof-based attribute offsets,
// keeps the vao setup code in sync, and makes vertex-format changes local to
// this header plus the shader declarations.

namespace Poseidon::render::vao
{

// vsscreen reads tlvertex with six attributes. the caller must bind a vao and
// the matching vbo to gl_array_buffer first.
inline void SetupTLVertexLayout()
{
    const GLsizei stride = sizeof(TLVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, rhw)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(TLVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(TLVertex, specular)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, t0)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, t1)));
}

// vstransform reads svertex with three attributes. the caller must bind a vao
// and the matching vbo to gl_array_buffer first.
inline void SetupSVertexLayout()
{
    const GLsizei stride = sizeof(SVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SVertex, norm)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SVertex, t0)));
}

} // namespace Poseidon::render::vao
