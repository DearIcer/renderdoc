/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

#include "common/common.h"

// 3DMigoto-style COM vtable hooking.
//
// Rather than replacing a COM object with a wrapper (which changes the object
// pointer the application sees and breaks the assumptions of any other
// component that has already vtable-hooked the same object, such as an
// anti-cheat or overlay), we patch the individual vtable slot we care about.
//
// The slot keeps its identity: the object pointer returned to the game is
// unchanged, and any hook that was already installed in the slot becomes the
// "original" we call onwards, so our hook chains with existing hooks instead of
// fighting them. This is the same technique 3DMigoto uses for IDXGIFactory and
// IDXGISwapChain interception (see DirectX11/HookedDXGI.cpp and
// DirectX11/HookAddresses.c).
namespace VTableHook
{
// Returns the function pointer currently stored in slot `slot` of obj's vtable.
void *GetSlot(void *obj, size_t slot);

// Installs `detour` into slot `slot` of obj's vtable.
// `orig` receives the previous slot value, which the detour should call onwards
// to chain with any hook that was already installed.
//
// If `cloneVtable` is true a private copy of the vtable is allocated for this
// object and the object's vtable pointer is redirected to it, so the shared
// vtable in the owning module (e.g. dxgi.dll) is never modified and won't trip
// up code that checksums it. The private copy is owned by the object and is
// cleaned up by Remove().
//
// Returns false (and leaves the slot untouched) if obj is NULL, the slot
// already contains `detour`, or the memory protection could not be changed.
bool Install(void *obj, size_t slot, void *detour, void **orig = NULL, bool cloneVtable = false);

// Restores `orig` into slot `slot` of obj's vtable and, if a private vtable
// copy was created by Install(), restores the object's vtable pointer and
// frees the copy.
bool Remove(void *obj, size_t slot, void *orig);

// RAII helper that remembers (obj, slot, original) so the hook can be removed
// later without re-reading the (possibly re-hooked) slot.
class Scoped
{
public:
  Scoped();
  ~Scoped();

  // Installs the hook. See VTableHook::Install for parameters.
  bool Install(void *obj, size_t slot, void *detour, bool cloneVtable = false);

  // Restores the original slot value. Safe to call multiple times.
  bool Remove();

  bool IsInstalled() const { return m_Installed; }
  void *GetObject() const { return m_Obj; }
  size_t GetSlot() const { return m_Slot; }
  void *GetOriginal() const { return m_Orig; }

private:
  void *m_Obj;
  size_t m_Slot;
  void *m_Orig;
  bool m_Installed;
  bool m_ClonedVtable;
};
}    // namespace VTableHook
