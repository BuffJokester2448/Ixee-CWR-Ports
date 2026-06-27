#include <Poseidon/Core/Application.hpp>

#include <PoseidonGLES32/TextureGLES32.hpp>
#include <PoseidonGLES32/GLES32BindCache.hpp>
#include <PoseidonGLES32/EngineGLES32.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <PoseidonGLES32/GLESCompat.hpp>

#include <Poseidon/Graphics/Core/MipmapLayout.hpp>
#include <vector>

#ifdef __ANDROID__
#include "../../../thirdparty/bcdec.h"
#include "../../thirdparty/etcpak/ProcessRGB.hpp"
#endif

extern int MipmapSizeGLES32(PacFormat format, int w, int h);
extern void InitGLESPixelFormat(TextureDescGLES32& desc, PacFormat format, bool enableDXT);

#ifdef __ANDROID__
// convert packed ARGB pixel data to RGBA bytes for gles upload.
// returns true when conversion happened and outBuf was filled, false when the
// format is already native to gles.
static bool ConvertToRGBA(PacFormat fmt, const void* src, void* dst, int w, int h)
{
    int pixels = w * h;
    const uint8_t* s8 = static_cast<const uint8_t*>(src);
    uint8_t* d8 = static_cast<uint8_t*>(dst);

    switch (fmt)
    {
        case PacARGB1555:
        {
            // packed uint16: a(1) r(5) g(5) b(5) in little endian.
            const uint16_t* sp = reinterpret_cast<const uint16_t*>(src);
            for (int i = 0; i < pixels; i++)
            {
                uint16_t v = sp[i];
                int r = ((v >> 10) & 0x1F) * 255 / 31;
                int g = ((v >> 5) & 0x1F) * 255 / 31;
                int b = (v & 0x1F) * 255 / 31;
                int a = (v >> 15) ? 255 : 0;
                d8[i * 4 + 0] = static_cast<uint8_t>(r);
                d8[i * 4 + 1] = static_cast<uint8_t>(g);
                d8[i * 4 + 2] = static_cast<uint8_t>(b);
                d8[i * 4 + 3] = static_cast<uint8_t>(a);
            }
            return true;
        }
        case PacARGB4444:
        {
            // packed uint16: a(4) r(4) g(4) b(4) in little endian.
            const uint16_t* sp = reinterpret_cast<const uint16_t*>(src);
            for (int i = 0; i < pixels; i++)
            {
                uint16_t v = sp[i];
                int a = ((v >> 12) & 0xF) * 17;
                int r = ((v >> 8) & 0xF) * 17;
                int g = ((v >> 4) & 0xF) * 17;
                int b = (v & 0xF) * 17;
                d8[i * 4 + 0] = static_cast<uint8_t>(r);
                d8[i * 4 + 1] = static_cast<uint8_t>(g);
                d8[i * 4 + 2] = static_cast<uint8_t>(b);
                d8[i * 4 + 3] = static_cast<uint8_t>(a);
            }
            return true;
        }
        case PacARGB8888:
        {
            // little-endian layout is B(0) G(1) R(2) A(3), so rewrite to R G B A.
            for (int i = 0; i < pixels; i++)
            {
                d8[i * 4 + 0] = s8[i * 4 + 2]; // R.
                d8[i * 4 + 1] = s8[i * 4 + 1]; // G.
                d8[i * 4 + 2] = s8[i * 4 + 0]; // B.
                d8[i * 4 + 3] = s8[i * 4 + 3]; // A.
            }
            return true;
        }
        default:
            return false;
    }
}
#endif

void TextureGLES32::InitDesc(TextureDescGLES32& desc, int levelMin, bool enableDXT)
{
    memset(&desc, 0, sizeof(desc));

    PacFormat format = _mipmaps[levelMin].DstFormat();
    InitGLESPixelFormat(desc, format, enableDXT);

    desc.w = _mipmaps[levelMin]._w;
    desc.h = _mipmaps[levelMin]._h;
    desc.nMipmaps = _nMipmaps - levelMin;
}

int TextureGLES32::TotalSize(int levelMin) const
{
    int totalSize = 0;
    for (int i = levelMin; i < _nMipmaps; i++)
    {
        const PacLevelMem& mip = _mipmaps[i];
        totalSize += MipmapSizeGLES32(mip.DstFormat(), mip._w, mip._h);
    }
    return totalSize;
}

int TextureGLES32::UploadToGPU(SurfaceInfoGLES32& surface, int levelMin)
{
    if (!_src)
    {
        RptF("No texture source for %s", Name());
        return -1;
    }

    unsigned int tex = surface.GetTexture();
    if (!tex)
        return -1;

    // upload via the dedicated upload unit so the cached unit-0 and unit-1
    // bindings remain accurate.
    // otherwise a demand-load between two draws can leave gl bound to the
    // freshly uploaded handle while the cache still reports the previous one.
    GLES32Bind::Tex2D(EngineGLES32::kUploadUnit - GL_TEXTURE0, tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // capture the format once at level-min and reuse it for every mip.
    // per-mip DstFormat() can diverge from the level-min format, and
    // CreateSurface allocates every mip level with the level-min format.
    // uploading a sub-image with a different compressed internalFormat trips
    // GL_INVALID_OPERATION, so level-min remains authoritative.
    const PacFormat sharedFmt = _mipmaps[levelMin].DstFormat();

    for (int i = levelMin; i < _nMipmaps; i++)
    {
        PacLevelMem& mip = _mipmaps[i];
        int aLevel = i - levelMin;

        PacFormat dstFmt = sharedFmt;
        int tightPitch = 0;
        int rowCount = 0;
        int dataSize = 0;

        // compute per-mip pitch and size from this mip's dimensions, not the
        // base mip's.
        const auto layout = Poseidon::render::mipmap::ComputeLayout(dstFmt, mip._w, mip._h);
        tightPitch = layout.tightPitch;
        rowCount = layout.rowCount;
        dataSize = layout.dataSize;

        AUTO_STATIC_ARRAY(char, pixelData, 256 * 256 * 4);
        pixelData.Realloc(dataSize);
        pixelData.Resize(dataSize);

        int ret = _src->GetMipmapData(pixelData.Data(), mip, i);

        if (_interpolate)
        {
            PoseidonAssert(_interpolate->_nMipmaps == _nMipmaps);
            PacLevelMem& imip = _interpolate->_mipmaps[i];

            AUTO_STATIC_ARRAY(short, imem, 256 * 256);
            imem.Realloc(imip._w * imip._h);
            imem.Resize(imip._w * imip._h);

            _interpolate->_src->GetMipmapData(imem.Data(), imip, i);
            mip.Interpolate(pixelData.Data(), imem.Data(), imip, _iFactor);
        }

        if (!ret)
        {
            memset(pixelData.Data(), 0, dataSize);
            Poseidon::Foundation::WarningMessage("Cannot load mipmap %s", Name());
        }

        // upload to gl.
        // upload to gl.
        TextureDescGLES32 fmtDesc;
        InitGLESPixelFormat(fmtDesc, dstFmt, static_cast<EngineGLES32*>(GEngine)->CanDXT(1));

        if (fmtDesc.compressed)
        {
#ifdef __ANDROID__
            if (!static_cast<EngineGLES32*>(GEngine)->CanDXT(1) && 
                mip._sFormat >= PacDXT1 && mip._sFormat <= PacDXT5)
            {
                // decode DXT to RGBA with bcdec, then RGBA to ETC2 with etcpak.
                int blockW = (mip._w + 3) / 4;
                int blockH = (mip._h + 3) / 4;
                int blocks = blockW * blockH;
                bool isAlpha = (fmtDesc.internalFormat == GL_COMPRESSED_RGBA8_ETC2_EAC);
                
                std::vector<uint32_t> rgbaBuf(mip._w * mip._h);
                const uint8_t* src = reinterpret_cast<const uint8_t*>(pixelData.Data());
                int blockSize = (mip._sFormat == PacDXT1) ? 8 : 16;
                
                for (int by = 0; by < blockH; ++by)
                {
                    for (int bx = 0; bx < blockW; ++bx)
                    {
                        uint8_t rgba[64];
                        if (mip._sFormat == PacDXT1)
                            bcdec_bc1(src, rgba, 4 * 4);
                        else if (mip._sFormat == PacDXT2 || mip._sFormat == PacDXT3)
                            bcdec_bc2(src, rgba, 4 * 4);
                        else
                            bcdec_bc3(src, rgba, 4 * 4);
                        
                        src += blockSize;
                        
                        for (int py = 0; py < 4; ++py)
                        {
                            int y = by * 4 + py;
                            if (y >= mip._h) continue;
                            for (int px = 0; px < 4; ++px)
                            {
                                int x = bx * 4 + px;
                                if (x >= mip._w) continue;
                                
                                const uint8_t* p = &rgba[(py * 4 + px) * 4];
                                // bcdec outputs RGBA (p[0]=R,p[1]=G,p[2]=B,p[3]=A).
                                // etcpak reads uint32 bytes as B=0,G=1,R=2,A=3,
                                // so swap R and B when packing.
                                uint32_t c = (p[3] << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
                                rgbaBuf[y * mip._w + x] = c;
                            }
                        }
                    }
                }
                
                std::vector<uint64_t> etcBuf(blocks * (isAlpha ? 2 : 1));
                if (isAlpha)
                    CompressEtc2Rgba(rgbaBuf.data(), etcBuf.data(), blocks, mip._w, true);
                else
                    CompressEtc1Rgb(rgbaBuf.data(), etcBuf.data(), blocks, mip._w);
                
                glCompressedTexSubImage2D(GL_TEXTURE_2D, aLevel, 0, 0, mip._w, mip._h, fmtDesc.internalFormat, etcBuf.size() * sizeof(uint64_t), etcBuf.data());
            }
            else
#endif
            {
                glCompressedTexSubImage2D(GL_TEXTURE_2D, aLevel, 0, 0, mip._w, mip._h, fmtDesc.internalFormat, dataSize,
                                          pixelData.Data());
            }
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                LOG_ERROR(
                    Graphics,
                    "GLES32: glCompressedTexSubImage2D FAILED err=0x{:04X} tex={} level={} {}x{} fmt=0x{:04X} size={}",
                    err, tex, aLevel, mip._w, mip._h, fmtDesc.internalFormat, dataSize);
            }
        }
        else
        {
#ifdef __ANDROID__
            // gles cannot handle GL_BGRA or _REV pixel types.
            // convert packed ARGB formats to RGBA bytes on the cpu before upload.
            int rgbaSize = mip._w * mip._h * 4;
            std::vector<uint8_t> rgbaBuf(rgbaSize);
            const void* uploadPtr = pixelData.Data();
            if (ConvertToRGBA(dstFmt, pixelData.Data(), rgbaBuf.data(), mip._w, mip._h))
                uploadPtr = rgbaBuf.data();
            
            glTexSubImage2D(GL_TEXTURE_2D, aLevel, 0, 0, mip._w, mip._h, fmtDesc.pixelFormat, fmtDesc.pixelType,
                            uploadPtr);
#else
            glTexSubImage2D(GL_TEXTURE_2D, aLevel, 0, 0, mip._w, mip._h, fmtDesc.pixelFormat, fmtDesc.pixelType,
                            pixelData.Data());
#endif
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                LOG_ERROR(Graphics, "GLES32: glTexSubImage2D FAILED err=0x{:04X} tex={} level={} {}x{} fmt=0x{:04X}", err,
                          tex, aLevel, mip._w, mip._h, fmtDesc.pixelFormat);
            }
        }
    }

    GLES32Bind::ActiveUnit(0);
    return 0;
}

int TextureGLES32::LoadLevels(int levelMin)
{
    if (levelMin < 0)
        return 0;

    TextBankGLES32* bank = static_cast<TextBankGLES32*>(GEngine->TextBank());

    PoseidonAssert(levelMin < _nMipmaps);
    PoseidonAssert(levelMin >= 0);

    int ret = 0;

    if (_interpolate)
        _interpolate->_inUse++;

    if (_levelLoaded > levelMin)
    {
        ReleaseMemory(true);

        _inUse++;

        TextureDescGLES32 desc;
        InitDesc(desc, levelMin, static_cast<EngineGLES32*>(GEngine)->CanDXT(1));

        PacFormat format = _mipmaps[levelMin].DstFormat();
        bank->UseReleased(_surface, desc, format);

        if (!_surface.GetTexture())
        {
            if (bank->_totalAllocated > bank->_limitAllocatedTextures - 512 * 1024)
                bank->Reuse(_surface, desc, format);
        }

        int totalSize = TotalSize(levelMin);

        if (!_surface.GetTexture())
        {
            bank->ReserveMemory(totalSize);

            if (bank->CreateGPUSurface(_surface, desc, format, totalSize) < 0)
            {
                _inUse--;
                if (_interpolate)
                    _interpolate->_inUse--;
                return -1;
            }

            bank->_thisFrameAlloc++;
        }

        ret = UploadToGPU(_surface, levelMin);

        _inUse--;

        CacheUse(bank->_thisFrameWholeUsed);
        _levelLoaded = levelMin;
    }

    if (_interpolate)
        _interpolate->_inUse--;

    if (ret < 0)
        ReleaseMemory(true);

    return ret;
}

void TextureGLES32::ReleaseSmall(bool store)
{
    TextBankGLES32* bank = static_cast<TextBankGLES32*>(GEngine->TextBank());
    if (_smallSurface.GetTexture())
    {
        if (store)
        {
            bank->AddReleased(_smallSurface);
            _smallSurface.Free(false);
        }
        else
        {
            bank->_totalAllocated -= _smallSurface.SizeUsed();
            bank->_thisFrameAlloc++;
            _smallSurface.Free(true);
        }
        _smallLoaded = _nMipmaps;
    }
}

int TextureGLES32::LoadSmall()
{
    if (_smallSurface.GetTexture())
        return 0;

    TextBankGLES32* bank = static_cast<TextBankGLES32*>(GEngine->TextBank());

    if (_nMipmaps <= 0)
        return -1;

    int i;
    for (i = 0; i < _nMipmaps; i++)
    {
        int pixels = _mipmaps[i]._w * _mipmaps[i]._h;
        if (pixels <= bank->_maxSmallTexturePixels)
            break;
    }
    int levelMin = i;
    if (levelMin >= _nMipmaps)
        levelMin = _nMipmaps - 1;

    PacFormat format = _mipmaps[levelMin].DstFormat();
    TextureDescGLES32 desc;
    InitDesc(desc, levelMin, false);

    bank->UseReleased(_smallSurface, desc, format);

    int totalSize = TotalSize(levelMin);

    if (!_smallSurface.GetTexture())
    {
        bank->ReserveMemory(totalSize);

        if (bank->CreateGPUSurface(_smallSurface, desc, format, totalSize) < 0)
            return -1;
    }

    int ret = UploadToGPU(_smallSurface, levelMin);
    if (ret >= 0)
    {
        _smallLoaded = levelMin;
        return 0;
    }
    return -1;
}

void TextureGLES32::MemoryReleased()
{
    if (_cache)
    {
        _cache->Delete();
        delete _cache;
        _cache = nullptr;
    }
    _levelLoaded = _nMipmaps;
}

void TextureGLES32::ReleaseMemory(bool store)
{
    TextBankGLES32* bank = static_cast<TextBankGLES32*>(GEngine->TextBank());
    if (_surface.GetTexture())
    {
        if (store)
        {
            bank->AddReleased(_surface);
        }
        else
        {
            bank->_totalAllocated -= _surface.SizeUsed();
            bank->_thisFrameAlloc++;
        }
        _surface.Free(!store);
        MemoryReleased();
    }
}

void TextureGLES32::ReuseMemory(SurfaceInfoGLES32& surf)
{
    if (_surface.GetTexture())
    {
        surf = _surface;
        _surface.Free(false);
    }
    MemoryReleased();
}

void TextureGLES32::CacheUse(GLES32MipCacheRoot& list)
{
    HMipCacheGLES32* first;
    if (_cache)
    {
        _cache->Delete();
        first = _cache;
    }
    else
    {
        first = new HMipCacheGLES32;
    }
    first->texture = this;
    list.Insert(first);
    _cache = first;
}
