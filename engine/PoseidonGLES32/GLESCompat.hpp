// opengl es 3.2 compatibility layer for the poseidongles32 backend.
// 
// handles the differences between desktop gl 3.3 core and gles 3.2 so the
// rest of the backend code can stay as close to the gl33 original as possible.
// 
// the big ones are
// gldrawbuffer / glreadbuffer(gl_none) not existing in gles
// glcleardepth taking a double on desktop but gles only has glcleardepthf
// glgetteximage not existing at all in gles
// gl_bgra and the _rev packed pixel types not being valid in gles
// s3tc / dxt compressed formats not being guaranteed on mobile gpus

#pragma once

#include <glad/gles2.h>

// glcleardepth takes a double on desktop gl. gles only has glcleardepthf
// which takes a float. the shadow depth path calls glcleardepth(1.0) so
// we just redirect it.
#ifndef glClearDepth
#define glClearDepth(d) glClearDepthf(static_cast<float>(d))
#endif

// gldrawbuffer / glreadbuffer with gl_none are used on desktop gl to mark
// depth only fbos that have no color attachment. gles does not have these
// calls at all, and depth only fbos are valid without them. we just make
// the call sites compile as no ops.
inline void glesDrawBufferNone() {}
inline void glesReadBufferNone() {}

// glgetteximage does not exist in gles. the gl33 backend only uses it in
// dumpshadowmap which is a debug diagnostic, so we stub it out. the gles
// backend's dumpshadowmap simply returns false.
inline void glesGetTexImageStub(unsigned int /*target*/, int /*level*/,
                                unsigned int /*format*/, unsigned int /*type*/,
                                void* /*pixels*/)
{
}

// glreadpixels with gl_depth_component is not allowed in gles. the gl33
// backend uses it in shadowdepthprobe which is a test only path. on gles
// we skip the depth readback entirely.
#ifndef GLES_NO_DEPTH_READBACK
#define GLES_NO_DEPTH_READBACK 1
#endif

// gl_texture_max_anisotropy_ext might not be in the gles headers but most
// arm gpus (mali, adreno) do support the extension at runtime.
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// s3tc / dxt enums. not in the gles headers unless the driver exposes
// ext_texture_compression_s3tc. we define them here so the texture format
// code compiles. at runtime we check the extension string before using them,
// and if s3tc is not available we decompress to rgba on the cpu instead.
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// gl_bgra is not a valid pixel format in gles core. texture uploads need
// to use gl_rgba with cpu swizzled data. the enum is defined here only so
// format selection code compiles. the actual upload path detects this and
// does the byte swap before calling gltexsubimage2d.
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

// reversed packed pixel types do not exist in gles. desktop gl uses these
// for argb1555, argb4444, and argb8888 textures. on gles the upload path
// catches these values and byte swaps the pixel data before uploading with
// the gles native types instead.
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4_REV
#define GL_UNSIGNED_SHORT_4_4_4_4_REV 0x8365
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

// everything below is available in gles 3.0 or 3.2 core and needs no
// compatibility shims at all.
// 
// gldrawelementsinstanced, gl_instanceid, sampler2darray, glteximage3d,
// gltexstorage2d, gl_fragdepth, glrenderbufferstoragemultisample,
// glblitframebuffer, gl_sample_alpha_to_coverage, gl_texture_swizzle_*,
// khr_debug (core in gles 3.2)
