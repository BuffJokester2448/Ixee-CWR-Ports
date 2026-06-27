#include <PoseidonGLES32/EngineGLES32.hpp>

#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>

static inline float PlaneDistance2(const Plane& p1, const Plane& p2)
{
    float d = Square(p1.D() - p2.D());
    d += p1.Normal().Distance2(p2.Normal());
    return d;
}


// order-sensitive signature of a draw's light list.
// lights are static within a frame and the material cache resets at pass
// boundaries, so pointer identity is sufficient to tell whether two draws saw
// the same local lights.
static uint64_t LightsSignature(const LightList& lights)
{
    uint64_t sig = static_cast<uint64_t>(lights.Size());
    for (int i = 0; i < lights.Size(); i++)
        sig = sig * 1099511628211ull ^ reinterpret_cast<uintptr_t>(static_cast<const Light*>(lights[i]));
    return sig;
}

#ifndef NDEBUG
// signature of the frame-constant lighting inputs DoSetMaterial folds into its
// upload.
// MainLight NightEffect, sun diffuse and ambient, and sun-enable stay out of
// the per-draw cache key because they are constant within a frame and the cache
// is invalidated every frame.
// the debug tripwire in SetMaterial asserts that invariant holds.
static uint64_t MaterialFrameInputsSig(const render::LegacySpec& spec, bool sunEnabled)
{
    LightSun* sun = GScene->MainLight();
    float night = sun->NightEffect();
    if (static_cast<std::uint32_t>(spec.material & render::Material::DisableSun) != 0)
        night = 1.0f;
    const Color d = sun->Diffuse();
    const Color a = sun->Ambient();
    const float vals[] = {night, d.R(), d.G(), d.B(), d.A(), a.R(), a.G(), a.B(), a.A()};
    uint64_t h = sunEnabled ? 14695981039346656037ull : 1099511628211ull;
    for (float f : vals)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
}
#endif

// low-level material and light path.
// updates pixel-shader specular selection and uploads material constants.
void EngineGLES32::DoSetMaterial(const TLMaterial& mat, const LightList& lights, const Poseidon::render::LegacySpec& spec)
{
    _materialSet = mat;
    // the cache key only tracks the material bit SetMaterial actually compares
    // against: DisableSun.
    _materialSetSpec = static_cast<int>(static_cast<std::uint32_t>(spec.material & Poseidon::render::Material::DisableSun));
    _materialSetLightsSig = LightsSignature(lights);
#ifndef NDEBUG
    _materialFrameInputsSig = MaterialFrameInputsSig(spec, _sunEnabled);
#endif

    PROFILE_DX_SCOPE(3mat);

    UploadVSMaterialConstants(mat, _sunEnabled);

    // local lights illuminate geometry only at night; DisableSun materials,
    // which the legacy SetupLights forced to full night, always receive them.
    float night = GScene->MainLight()->NightEffect();
    if (static_cast<std::uint32_t>(spec.material & render::Material::DisableSun) != 0)
        night = 1.0f;
    UploadVSLights(lights, mat, night);

    if (mat.specularPower > 0)
        SelectPixelShaderSpecular(PSSSpecular);
    else
        SelectPixelShaderSpecular(PSSNormal);
}

// high-level material and light path with caching.
// avoids redundant DoSetMaterial calls when the active state already matches.
void EngineGLES32::SetMaterial(const TLMaterial& mat, const LightList& lights, const Poseidon::render::LegacySpec& spec)
{
    const int narrowedKey = static_cast<int>(static_cast<std::uint32_t>(spec.material & Poseidon::render::Material::DisableSun));
    if (mat == _materialSet && _materialSetSpec == narrowedKey && LightsSignature(lights) == _materialSetLightsSig)
    {
#ifndef NDEBUG
        // the cache key matched, so reuploading is skipped.
        // assert that the frame-constant lighting inputs read by the upload but
        // omitted from the key are still unchanged, which means the cache has
        // not outlived its frame.
        PoseidonAssert(MaterialFrameInputsSig(spec, _sunEnabled) == _materialFrameInputsSig);
#endif
        return;
    }
    DoSetMaterial(mat, lights, spec);
}

void EngineGLES32::EnableSunLight(bool enable)
{
    if (_sunEnabled == enable)
        return;
    _sunEnabled = enable;
    _frameState.sunEnabled = enable;
    if (!IsIn3DPass())
        return;
    UploadFrameConstants(_frameState);

    _materialSetSpec = -1;
    _materialSet.diffuse = Color(-1, -1, -1, -1);
    _materialSet.ambient = Color(-1, -1, -1, -1);
    _materialSet.forcedDiffuse = Color(-1, -1, -1, -1);
    _materialSet.emmisive = Color(-1, -1, -1, -1);
    _materialSet.specFlags = 0;
}
