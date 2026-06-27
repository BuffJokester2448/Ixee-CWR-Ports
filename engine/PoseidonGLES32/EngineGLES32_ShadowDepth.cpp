#include <PoseidonGLES32/EngineGLES32.hpp>
#include <PoseidonGLES32/GLES32BindCache.hpp>

#include <PoseidonGLES32/GLESCompat.hpp>

#include <Poseidon/Graphics/Core/GLClear.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLDepthStencilState.hpp>
#include <Poseidon/Graphics/Shared/PNGWriter.hpp>
#include <PoseidonGLES32/TextureGLES32.hpp>

#include <cstdint>
#include <vector>

// self-contained offscreen depth render used to validate the gl shadow-depth
// path against the cpu oracle.
// renders triangle geometry from the light into a depth fbo and reads the
// depth back.
// it is only invoked by the triShadowDepthProbe test verb, so it cannot affect
// normal rendering.

namespace
{
GLuint s_prog = 0;
GLint s_locVP = -1;
GLuint s_fbo = 0;
GLuint s_tex = 0;
int s_res = 0;
GLuint s_vao = 0;
GLuint s_vbo = 0;

// cascade depth-map array for the live lit path.
// the single-layer s_fbo and s_tex above stay for the ShadowDepthProbe cpu
// oracle cross-check test.
GLuint s_arrFbo = 0;
GLuint s_arrTex = 0;
int s_arrRes = 0;
int s_arrLayers = 0;

// alpha-tested caster pass: a second depth program and mesh that samples the
// caster texture alpha and discards so cutout foliage casts a leaf silhouette.
GLuint s_alphaProg = 0;
GLint s_alphaLocVP = -1;
GLint s_alphaLocTex = -1;
GLuint s_alphaVao = 0;
GLuint s_alphaVbo = 0;

struct ResolvedAlphaBatch
{
    GLuint handle;
    int firstVertex;
    int vertexCount;
};

const char* kVS = R"(#version 320 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(location = 0) in vec3 pos;
uniform mat4 uLightVP;
void main() { gl_Position = uLightVP * vec4(pos, 1.0); }
)";

const char* kFS = R"(#version 320 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
void main() {}
)";

GLuint CompileOne(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOG_ERROR(Graphics, "GL33 shadow-depth shader compile: {}", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool EnsureProgram()
{
    if (s_prog)
        return true;
    GLuint vs = CompileOne(GL_VERTEX_SHADER, kVS);
    GLuint fs = CompileOne(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs)
        return false;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(prog);
        return false;
    }
    s_prog = prog;
    s_locVP = glGetUniformLocation(prog, "uLightVP");
    return true;
}

bool EnsureTarget(int res)
{
    if (s_fbo && s_res == res)
        return true;
    if (s_tex)
    {
        GLES32Bind::OnTexDeleted(s_tex);
        glDeleteTextures(1, &s_tex);
        s_tex = 0;
    }
    if (s_fbo)
    {
        glDeleteFramebuffers(1, &s_fbo);
        s_fbo = 0;
    }

    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    GLES32Bind::Invalidate(); // raw init-path bind on an unknown unit.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &s_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_tex, 0);
    // GLES does not have glDrawBuffer, and depth-only FBOs are valid without it.
    // GLES does not have glReadBuffer for depth-only FBOs.
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERROR(Graphics, "GL33 shadow-depth FBO incomplete: {}", static_cast<unsigned>(status));
        return false;
    }
    s_res = res;
    return true;
}

bool EnsureArrayTarget(int res, int layers)
{
    if (s_arrFbo && s_arrRes == res && s_arrLayers == layers)
        return true;
    if (s_arrTex)
    {
        glDeleteTextures(1, &s_arrTex);
        s_arrTex = 0;
    }
    if (s_arrFbo)
    {
        glDeleteFramebuffers(1, &s_arrFbo);
        s_arrFbo = 0;
    }

    glGenTextures(1, &s_arrTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_arrTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, res, res, layers, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &s_arrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_arrFbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, s_arrTex, 0, 0);
    // GLES does not have glDrawBuffer, depth-only FBOs are valid without it
    // GLES does not have glReadBuffer for depth-only FBOs
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERROR(Graphics, "GL33 cascade FBO incomplete: {}", static_cast<unsigned>(status));
        return false;
    }
    s_arrRes = res;
    s_arrLayers = layers;
    return true;
}

bool EnsureMesh()
{
    if (s_vao)
        return true;
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    GLES32Bind::Vao(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    GLES32Bind::Vao(0);
    return true;
}

const char* kAlphaVS = R"(#version 320 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
uniform mat4 uLightVP;
out vec2 vUV;
void main() { vUV = uv; gl_Position = uLightVP * vec4(pos, 1.0); }
)";

const char* kAlphaFS = R"(#version 320 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
in vec2 vUV;
uniform sampler2D uTex;
void main() { if (texture(uTex, vUV).a < 0.5) discard; }
)";

bool EnsureAlphaProgram()
{
    if (s_alphaProg)
        return true;
    GLuint vs = CompileOne(GL_VERTEX_SHADER, kAlphaVS);
    GLuint fs = CompileOne(GL_FRAGMENT_SHADER, kAlphaFS);
    if (!vs || !fs)
        return false;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(prog);
        return false;
    }
    s_alphaProg = prog;
    s_alphaLocVP = glGetUniformLocation(prog, "uLightVP");
    s_alphaLocTex = glGetUniformLocation(prog, "uTex");
    return true;
}

bool EnsureAlphaMesh()
{
    if (s_alphaVao)
        return true;
    glGenVertexArrays(1, &s_alphaVao);
    glGenBuffers(1, &s_alphaVbo);
    GLES32Bind::Vao(s_alphaVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_alphaVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    GLES32Bind::Vao(0);
    return true;
}
} // namespace

namespace
{
// render vertCount world-space triangle vertices from the light into the depth
// fbo and optionally read the depth back.
// depth state goes through the core bundles so the gl-state audits stay green;
// ApplyPipeline re-owns depth and cull on the next draw, so only bindings are
// restored.
bool RenderDepthFBO(void* glContext, const float* lightVP16, const float* triXYZ, int vertCount, int res,
                    float* outDepthOrNull)
{
    if (!glContext || !lightVP16 || !triXYZ || vertCount < 3 || res <= 0)
        return false;
    if (!EnsureProgram() || !EnsureTarget(res) || !EnsureMesh())
        return false;

    GLint prevFBO = 0, prevProg = 0, prevVAO = 0, prevArrayBuf = 0;
    GLint prevVP[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_VIEWPORT, prevVP);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glViewport(0, 0, res, res);
    Poseidon::render::depthstencil::Normal(/*hasStencil*/ false); // test on, lequal, write on.
    Poseidon::render::cull::None();                               // single-map probe: capture both faces.
    glClearDepth(1.0);
    Poseidon::render::clear::WithMask(GL_DEPTH_BUFFER_BIT);

    glUseProgram(s_prog);
    glUniformMatrix4fv(s_locVP, 1, GL_FALSE, lightVP16);

    GLES32Bind::Vao(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertCount) * 3 * sizeof(float), triXYZ, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);

    // GL_DEPTH_COMPONENT float matches window-space depth in [0,1]; with
    // ZERO_TO_ONE clip control, window z matches ndc z and the cpu oracle.
    // origin is bottom-left.
    if (outDepthOrNull)
        glReadPixels(0, 0, res, res, GL_DEPTH_COMPONENT, GL_FLOAT, outDepthOrNull);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    glUseProgram(static_cast<GLuint>(prevProg));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    GLES32Bind::Vao(static_cast<GLuint>(prevVAO));
    return true;
}

// render the casters into each cascade layer of the depth array once per
// cascade with that cascade's light-vp.
// solid triangles use the depth-only program, then alpha-cutout batches use the
// texture-alpha-discard program.
// depth and clear go through the core bundles so the gl-state audits stay green;
// ApplyPipeline re-owns depth, cull, and texture on the next draw.
bool RenderCascadeArray(void* glContext, const float* lightVPs, int numCascades, int res, const float* solidXYZ,
                        int solidVertCount, const float* alphaXYZUV, int alphaVertCount,
                        const ResolvedAlphaBatch* batches, int batchCount)
{
    const bool haveSolid = solidXYZ && solidVertCount >= 3;
    const bool haveAlpha = alphaXYZUV && alphaVertCount >= 3 && batches && batchCount > 0;
    if (!glContext || !lightVPs || res <= 0 || numCascades < 1 || (!haveSolid && !haveAlpha))
        return false;
    if (!EnsureProgram() || !EnsureArrayTarget(res, numCascades) || !EnsureMesh())
        return false;
    if (haveAlpha && (!EnsureAlphaProgram() || !EnsureAlphaMesh()))
        return false;

    GLint prevFBO = 0, prevProg = 0, prevVAO = 0, prevArrayBuf = 0;
    GLint prevVP[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_VIEWPORT, prevVP);

    glBindFramebuffer(GL_FRAMEBUFFER, s_arrFbo);
    glViewport(0, 0, res, res);
    Poseidon::render::depthstencil::Normal(/*hasStencil*/ false);
    Poseidon::render::cull::None();

    // upload the cascade-invariant geometry once; only the light-vp changes per layer.
    if (haveSolid)
    {
        GLES32Bind::Vao(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(solidVertCount) * 3 * sizeof(float), solidXYZ,
                     GL_DYNAMIC_DRAW);
    }
    if (haveAlpha)
    {
        GLES32Bind::Vao(s_alphaVao);
        glBindBuffer(GL_ARRAY_BUFFER, s_alphaVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(alphaVertCount) * 5 * sizeof(float), alphaXYZUV,
                     GL_DYNAMIC_DRAW);
    }

    glClearDepth(1.0);
    for (int i = 0; i < numCascades; i++)
    {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, s_arrTex, 0, i);
        Poseidon::render::clear::WithMask(GL_DEPTH_BUFFER_BIT);

        if (haveSolid)
        {
            // store the back faces by culling front faces.
            // a lit front-facing surface is then strictly nearer the light than
            // the stored depth, which prevents self-shadow acne and keeps the
            // bias small.
            Poseidon::render::cull::Front();
            glUseProgram(s_prog);
            glUniformMatrix4fv(s_locVP, 1, GL_FALSE, lightVPs + i * 16);
            GLES32Bind::Vao(s_vao);
            glDrawArrays(GL_TRIANGLES, 0, solidVertCount);
        }
        if (haveAlpha)
        {
            Poseidon::render::cull::None(); // cutout foliage is two-sided.
            glUseProgram(s_alphaProg);
            glUniformMatrix4fv(s_alphaLocVP, 1, GL_FALSE, lightVPs + i * 16);
            glUniform1i(s_alphaLocTex, 0);
            GLES32Bind::ActiveUnit(0);
            GLES32Bind::Vao(s_alphaVao);
            for (int b = 0; b < batchCount; b++)
            {
                if (batches[b].vertexCount < 3)
                    continue;
                GLES32Bind::Tex2D(0, batches[b].handle);
                glDrawArrays(GL_TRIANGLES, batches[b].firstVertex, batches[b].vertexCount);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    glUseProgram(static_cast<GLuint>(prevProg));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    GLES32Bind::Vao(static_cast<GLuint>(prevVAO));
    return true;
}
} // namespace

bool EngineGLES32::ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res, float* outDepth)
{
    if (!outDepth)
        return false;
    const bool ok = RenderDepthFBO(_glContext, lightVP16, triXYZ, vertCount, res, outDepth);
    // RenderDepthFBO writes cull and depth directly and restores only bindings.
    // drop the pass-dedup cache so a later lit draw re-applies its raster state.
    InvalidatePipelineCache();
    return ok;
}

void EngineGLES32::RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3,
                                        int numCascades, int omniCount, int res, const ShadowCasterSet& casters)
{
    if (numCascades > kShadowCascades)
        numCascades = kShadowCascades;

    // resolve each alpha batch's caster texture to a gl handle, loading the base
    // mip if the depth pass beats the lit draw to it.
    std::vector<ResolvedAlphaBatch> resolved;
    resolved.reserve(static_cast<size_t>(casters.alphaBatchCount));
    for (int b = 0; b < casters.alphaBatchCount; b++)
    {
        const ShadowCasterBatch& src = casters.alphaBatches[b];
        TextureGLES32* t33 = static_cast<TextureGLES32*>(src.texture);
        if (_textBank && t33)
            _textBank->UseMipmap(t33, 0, 0);
        GLuint handle = t33 ? t33->GetHandle() : _fallbackWhiteTex;
        if (handle == 0)
            handle = _fallbackWhiteTex;
        resolved.push_back({handle, src.firstVertex, src.vertexCount});
    }

    const bool rendered = numCascades >= 1 &&
                          RenderCascadeArray(_glContext, lightVPs, numCascades, res, casters.solidXYZ,
                                             casters.solidVertexCount, casters.alphaXYZUV, casters.alphaVertexCount,
                                             resolved.data(), static_cast<int>(resolved.size()));
    if (numCascades >= 1)
    {
        // the cascade loop writes cull::Front and cull::None directly, and
        // RenderCascadeArray restores only bindings, not raster state.
        // drop the effort-06 pass-dedup cache so the next lit draw re-applies
        // its own cull via ApplyPipeline instead of inheriting front-face cull.
        InvalidatePipelineCache();
    }
    if (!rendered)
    {
        _shadowMapActive = false;
        return;
    }
    _shadowMapTex = s_arrTex;
    _shadowMapRes = res;
    _shadowCascades = numCascades;
    _shadowOmniCount = (omniCount < 0) ? 0 : (omniCount > numCascades ? numCascades : omniCount);
    for (int i = 0; i < numCascades * 16; i++)
    {
        _shadowMapVP[i] = lightVPs[i];
    }
    for (int i = 0; i < numCascades; i++)
    {
        _shadowSplits[i] = splitViewDist[i];
    }
    _shadowCamFwd[0] = camFwd3[0];
    _shadowCamFwd[1] = camFwd3[1];
    _shadowCamFwd[2] = camFwd3[2];
    _shadowMapActive = true;
}

bool EngineGLES32::DumpShadowMap(const char* /*path*/)
{
    return false;
};

bool EngineGLES32::ShadowMapCacheSelfTest()
{
    if (!_glContext)
        return true; // no gl context; not applicable, so do not fail the suite.

    const bool savedActive = _shadowMapActive;

    // prime the pass-dedup cache as if a lit draw had just set it, then run a
    // one-cascade depth pass on a tiny caster.
    // the depth pass leaves cull::Front behind; the next identical-descriptor
    // lit draw would short-circuit ApplyPipeline and inherit it unless the depth
    // pass invalidated the cache.
    _lastApplied.valid = true;

    const float lightVP[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float tri[9] = {-1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f};
    const float splits[1] = {10.0f};
    const float fwd[3] = {0.0f, 0.0f, 1.0f};
    ShadowCasterSet cs;
    cs.solidXYZ = tri;
    cs.solidVertexCount = 3;

    RenderShadowDepthScene(lightVP, splits, fwd, /*numCascades*/ 1, /*omniCount*/ 0, /*res*/ 256, cs);

    const bool invalidated = !_lastApplied.valid;
    _shadowMapActive = savedActive;
    return invalidated;
}
