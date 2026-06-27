// redundant bind elimination for the gles32 backend.
//
// the implementation mirrors the gl33 cache, but it lives in a separate
// namespace so both backends can be linked into the same binary without odr
// conflicts.
//
// all vao and 2d texture binds in this backend route through these helpers.
// any code that mutates bind state behind the cache's back must call
// invalidate() before the next draw.

#pragma once

namespace Poseidon
{
namespace GLES32Bind
{

void Vao(unsigned int vao);
void Tex2D(int unit, unsigned int tex); // keeps the active texture unit on unit.
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
