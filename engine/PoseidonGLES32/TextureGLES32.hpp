#ifdef _MSC_VER
#pragma once
#endif

#ifndef __TEXTURE_GL33_HPP
#define __TEXTURE_GL33_HPP

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>

class TextureGLES32;
class EngineGLES32;

class HMipCacheGLES32 : public CLDLink
{
  public:
    TextureGLES32* texture;

    USE_FAST_ALLOCATOR;
};

typedef HMipCacheGLES32 HSysCacheGLES32;

typedef CLList<HMipCacheGLES32> GLES32MipCacheRoot;
typedef GLES32MipCacheRoot GLES32SysCacheRoot;

struct TextureDescGLES32
{
    int w, h;
    int nMipmaps;
    unsigned int internalFormat; // GL internal format (e.g. GL_RGBA8)
    unsigned int pixelFormat;    // GL pixel format (e.g. GL_BGRA)
    unsigned int pixelType;      // GL pixel type (e.g. GL_UNSIGNED_BYTE)
    bool compressed;
};

class SurfaceInfoGLES32
{
  private:
    unsigned int _texture = 0; // GLuint texture object

    static int _nextId;
    int _id;

  public:
    int _totalSize;
    int _usedSize;
    int _w, _h;
    int _nMipmaps;
    Poseidon::PacFormat _format;

    int SizeExpected() const { return _totalSize; }
    int SizeUsed() const { return _usedSize; }
    int GetCreationID() const { return _id; }

    static int CalculateSize(const TextureDescGLES32& desc, Poseidon::PacFormat format, int totalSize = -1);
    int CreateSurface(const TextureDescGLES32& desc, Poseidon::PacFormat format, int totalSize = -1);
    void Free(bool lastRef, int refValue = 0);

    unsigned int GetTexture() const { return _texture; }
    void SetTexture(unsigned int tex) { _texture = tex; }
};

#define ASSERT_INIT() PoseidonAssert(_initialized);

class TextureGLES32 : public Texture
{
    typedef Texture base;

    friend class TextBankGLES32;
    friend class EngineGLES32;

  private:
    SRef<Poseidon::ITextureSource> _src;

    Ref<TextureGLES32> _interpolate;
    float _iFactor;

    bool _isDetail;
    bool _useDetail;
    bool _initialized;
    bool _dynamicMipmapped = false; // UpdateRGBA regenerates the mip chain when true

    int _maxSize;

    int _nMipmaps;
    PacLevelMem _mipmaps[MAX_MIPMAPS];

    SurfaceInfoGLES32 _surface;      // GPU texture
    SurfaceInfoGLES32 _smallSurface; // small GPU texture

    signed char _alphaClass = -1; // cached GetAlphaClass() verdict (-1 = not computed)

    signed char _largestUsed;
    signed char _smallLoaded;
    signed char _levelLoaded;
    signed char _levelNeededThisFrame;
    signed char _levelNeededLastFrame;
    signed char _inUse;

    HMipCacheGLES32* _cache;

    unsigned int GetSmallHandle() const { return _smallSurface.GetTexture(); }
    unsigned int GetBigHandle() const { return _surface.GetTexture(); }

    int LevelNeeded() const
    {
        return (_levelNeededThisFrame < _levelNeededLastFrame ? _levelNeededThisFrame : _levelNeededLastFrame);
    }

  public:
    TextureGLES32();
    ~TextureGLES32() override;

    void InitDesc(TextureDescGLES32& desc, int levelMin, bool enableDXT);

    int LoadLevels(int levelMin);
    int UploadToGPU(SurfaceInfoGLES32& surface, int levelMin);

    unsigned int GetHandle() const
    {
        unsigned int handle = GetBigHandle();
        return handle ? handle : GetSmallHandle();
    }

    const SurfaceInfoGLES32& GetSurface() const { return _surface.GetTexture() ? _surface : _smallSurface; }

  private:
    void MemoryReleased();
    Poseidon::AlphaStats::Kind ScanTopMipAlphaClass(); // decode top mip + ClassifyAlpha (multi-bit alpha only)

    int TotalSize(int levelMin) const;
    void ReleaseSmall(bool store = false);
    int LoadSmall();

  public:
    void ReleaseMemory(bool store = false);
    void ReuseMemory(SurfaceInfoGLES32& surf);

    bool IsAlpha() const override
    {
        ASSERT_INIT();
        return _src && _src->IsAlpha();
    }
    int AMaxSize() const override { return _maxSize; }
    void SetMaxSize(int size) override;
    void SetMultitexturing(int type) override;

    bool VerifyChecksum(const Poseidon::MipInfo& mip) const override;

    void SetMipmapRange(int min, int max) override;

    int Init(const char* name);

    // Dynamic texture from raw RGBA pixel data (font atlas, etc.).
    bool InitFromRGBA(int w, int h, const void* rgba, uint32_t size, bool mipmap = false);
    void UpdateRGBA(const void* rgba, uint32_t size);
    void DoLoadHeaders();
    void PreloadHeaders();

    void LoadHeadersNV() const
    {
        if (_initialized)
            return;
        const_cast<TextureGLES32*>(this)->DoLoadHeaders();
    }

    void LoadHeaders() override;

    const PacLevelMem* Mipmap(int level) const
    {
        ASSERT_INIT();
        return &_mipmaps[level];
    }

    const AbstractMipmapLevel& AMipmap(int level) const override
    {
        ASSERT_INIT();
        return _mipmaps[level];
    }
    AbstractMipmapLevel& AMipmap(int level) override
    {
        ASSERT_INIT();
        return _mipmaps[level];
    }

    int NMipmaps() const
    {
        ASSERT_INIT();
        return _nMipmaps;
    }
    int ANMipmaps() const override
    {
        ASSERT_INIT();
        return _nMipmaps;
    }
    void ASetNMipmaps(int n) override;
    int AWidth(int level = 0) const override
    {
        LoadHeadersNV();
        return _mipmaps[level]._w;
    }
    int AHeight(int level = 0) const override
    {
        LoadHeadersNV();
        return _mipmaps[level]._h;
    }
    bool IsTransparent() const override
    {
        ASSERT_INIT();
        return _src && _src->IsTransparent();
    }

    Poseidon::AlphaStats::Kind GetAlphaClass() override;

    Color GetPixel(int level, float u, float v) const override;
    Color GetColor() override
    {
        LoadHeaders();
        return _src ? _src->GetAverageColor() : HBlack;
    }

    int Width(int level) const
    {
        ASSERT_INIT();
        return _mipmaps[level]._w;
    }
    int Height(int level) const
    {
        ASSERT_INIT();
        return _mipmaps[level]._h;
    }

    void CacheUse(GLES32MipCacheRoot& list);

    NoCopy(TextureGLES32);
    USE_FAST_ALLOCATOR
};

class TextBankGLES32 : public AbstractTextBank
{
    typedef AbstractTextBank base;

    friend class TextureGLES32;

    int _maxTextureMemory;
    int _limitAllocatedTextures;
    int _reserveTextureMemory;

    int _maxSmallTexturePixels;

    LLinkArray<TextureGLES32> _texture;
    Ref<TextureGLES32> _detail;
    Ref<TextureGLES32> _specular;
    Ref<TextureGLES32> _grass;
    Ref<TextureGLES32> _waterBump;

    AutoArray<SurfaceInfoGLES32> _freeTextures;

    int _totalAllocated;

    EngineGLES32* _engine;

    // reports texture residency to the global memory manager. gpu eviction
    // remains render-thread only and is handled by the per-frame lru.
    Poseidon::Foundation::MemoryDomainProbe _memProbe;

    GLES32MipCacheRoot _thisFrameWholeUsed;
    GLES32MipCacheRoot _lastFrameWholeUsed;
    GLES32MipCacheRoot _thisFramePartialUsed;
    GLES32MipCacheRoot _lastFramePartialUsed;
    GLES32MipCacheRoot _previousUsed;

    int _thisFrameCopied;
    int _loadBoostFrames = 0; // frames left with the mip-upload budget lifted
    int _thisFrameAlloc;

  public:
    TextBankGLES32(EngineGLES32* engine);
    ~TextBankGLES32() override;

    int FreeTextureMemory();

  private:
    void CheckTextureMemory();
    void InitDetailTextures();

  public:
    int NTextures() const override { return _texture.Size(); }
    Texture* GetTexture(int i) const override { return _texture[i]; }

    void Compact() override;

    void Preload() override;
    void FlushTextures() override;
    void ForceReloadAll() override; // viewer F5 hot-reload — see TextBankGLES32_Cache.cpp
    void FlushBank(QFBank* bank) override;

  protected:
    int FindFree();

    TextureGLES32* Copy(int from);
    int Find(RStringB name1, TextureGLES32* interpolate = nullptr);

    int FindSurface(int w, int h, int nMipmaps, Poseidon::PacFormat format,
                    const AutoArray<SurfaceInfoGLES32>& array) const;

    int FindReleased(int w, int h, int nMipmaps, Poseidon::PacFormat format) const
    {
        return FindSurface(w, h, nMipmaps, format, _freeTextures);
    }
    void AddReleased(SurfaceInfoGLES32& surf);
    void UseReleased(SurfaceInfoGLES32& surf, const TextureDescGLES32& desc, Poseidon::PacFormat format);

    void Reuse(SurfaceInfoGLES32& surf, const TextureDescGLES32& desc, Poseidon::PacFormat format);
    void DeleteLastReleased();

  public:
    Ref<Texture> Load(RStringB name) override;
    Ref<Texture> LoadInterpolated(RStringB n1, RStringB n2, float factor) override;

    Texture* CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool mipmap = false) override;
    void UpdateDynamic(Texture* tex, const void* rgba, uint32_t size) override;

    void StopAll();

    void StartFrame() override;
    void BoostLoadBudget(int frames) override
    {
        if (frames > _loadBoostFrames)
            _loadBoostFrames = frames;
    }
    void FinishFrame() override;

    Poseidon::MipInfo UseMipmap(Texture* texture, int level, int levelTop) override;
    TextureGLES32* GetDetailTexture() const { return _detail; }
    TextureGLES32* GetGrassTexture() const { return _grass; }
    TextureGLES32* GetSpecularTexture() const { return _specular; }
    TextureGLES32* GetWaterBumpMap() const { return _waterBump; }

    bool VerifyChecksums() override;
    bool ReserveMemory(GLES32MipCacheRoot& root, int limit);
    bool ReserveMemory(int size);
    bool ForcedReserveMemory(int size);
    void ReleaseAllTextures() override;

    int CreateGPUSurface(SurfaceInfoGLES32& surface, const TextureDescGLES32& desc, Poseidon::PacFormat format,
                         int totalSize);

    void ReportTextures(const char* name);

    NoCopy(TextBankGLES32);
};

#endif
