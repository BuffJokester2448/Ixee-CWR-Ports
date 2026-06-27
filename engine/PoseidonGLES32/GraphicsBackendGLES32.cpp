// registers the gles 3.2 graphics backend with the engine factory.
//
// on android this is the primary backend. on desktop linux it can coexist
// with gl33 in theory, but the build is not structured around that path.

#include <Poseidon/Graphics/GraphicsEngineFactory.hpp>
#include <Poseidon/Graphics/Core/EngineFactory.hpp>
#include <Poseidon/Graphics/Shared/WindowMetrics.hpp>
using Poseidon::Engine;

using Poseidon::GraphicsBackendDescriptor;

using Poseidon::GraphicsEngineParams;
using Poseidon::GraphicsEngineFactory;

using Poseidon::CreateEngineGLES32;
using Poseidon::WindowMetrics;

namespace
{
Engine* CreateGLES32Backend(const GraphicsEngineParams& params)
{
    return CreateEngineGLES32(params.width, params.height, params.useWindow, params.bitsPerPixel);
}

bool IsGLES32Available()
{
    // android builds assume gles 3.2 availability. a stricter check could
    // query egl before committing to this backend.
    return true;
}
} // namespace

namespace Poseidon {
void RegisterGLES32GraphicsBackend()
{
    GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "gles32",
        "OpenGL ES 3.2 (SDL3)",
        200, // higher priority than gl33 on android.
        &CreateGLES32Backend,
        &IsGLES32Available,
    });
}
} // namespace poseidon
