# poseidon gles32 backend

this is the opengl es 3.2 renderer for poseidon, built for android and linux arm targets. it drops fixed-function pipelines entirely and maps everything to shaders.

## how it works

- state caching: the driver is slow on mobile. we pack blend modes, depth states, and bound textures into a descriptor and diff it against the hardware state before drawing. if nothing changed, we skip the driver call.
- geometry buffering: we don't dispatch thousands of tiny ui triangles one by one. vertices are stored in a cpu mirror and flushed to the gpu in bulk right before the draw call.
- instancing: dense meshes (like trees) are intercepted and grouped into instanced draws using a transform buffer.
- texture units: unit 0 and 1 are for active rendering. unit 7 is reserved strictly for async uploads so background streaming doesn't overwrite active bindings.

## mobile quirks

desktop gl is fundamentally different from gles. we handle the fallbacks quietly in `GLESCompat.hpp`.

### dxt to etc2 transcoding

we use standard dxt formats (dxt1, dxt3, dxt5). adreno gpus support this out of the box, but mali gpus do not. if we upload dxt to a mali chip, it either fails or eats massive amounts of vram.

to fix this, we intercept texture loads on mali hardware. we decompress the dxt blocks back to rgba on the cpu using `bcdec`, then immediately recompress them into etc2 blocks using `etcpak`. etc2 is natively supported everywhere. this keeps vram usage low without needing a separate asset pipeline.

### format conversions

gles 3.2 doesn't understand formats like `gl_bgra` or reverse packed pixels. we detect these during upload, swap the bytes on the cpu, and push them as standard `gl_rgba`.

### driver spam

adreno drivers vomit performance warnings into the debug log continuously. we configure the debug callback to filter out low-severity messages and notifications so you can actually read the logcat.

## shaders

uniform buffers are partitioned by update frequency. vertex shader slots are strictly mapped so 2d ui elements can't clobber the 3d projection matrix. local lights (like streetlamps) are packed directly into the vertex shader buffer for night lighting.

## supersampling

we allocate a 4x multisampled high-resolution buffer, render the scene into it, and downsample it back to the swapchain size at the end of the frame. it cleans up edges on high dpi screens without temporal ghosting.
