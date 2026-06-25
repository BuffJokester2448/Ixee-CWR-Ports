// redundant bind elimination for the gles32 backend.
// same logic as the gl33 bind cache but in its own namespace so both
// backends can coexist in the same link unit without odr conflicts.
// 
// every glbindvertexarray and glbindtexture(gl_texture_2d) in this backend
// goes through these helpers. anything that touches bind state outside
// them (context reset, external overlay) must call invalidate().

#pragma once

namespace Poseidon
{
namespace GLES32Bind
{

void Vao(unsigned int vao);
void Tex2D(int unit, unsigned int tex); // leaves the active unit on `unit`
void ActiveUnit(int unit);
void OnVaoDeleted(unsigned int vao);
void OnTexDeleted(unsigned int tex);
void Invalidate();
bool IsTexBound(int unit, unsigned int tex);

unsigned long long VaoRequests();
unsigned long long VaoBinds();
unsigned long long TexRequests();
unsigned long long TexBinds();

} // namespace gles32bind
} // namespace poseidon
