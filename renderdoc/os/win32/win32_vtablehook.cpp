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

#include "win32_vtablehook.h"

#include <windows.h>

#include <map>

#include "common/threading.h"

namespace VTableHook
{
namespace
{
Threading::CriticalSection s_Lock;

struct CloneInfo
{
  void *origVtbl = NULL;
  void *clonePage = NULL;
  uint32_t refs = 0;
};

// Objects whose vtable was cloned so the shared module vtable stays untouched.
// keyed by object pointer, refcounted so multiple slots on the same object can
// share one private vtable.
std::map<void *, CloneInfo> s_Clones;

const size_t kClonePageSize = 0x1000;

void *GetPageBase(void *p)
{
  return (void *)((uintptr_t)p & ~(uintptr_t)(kClonePageSize - 1));
}

bool PatchSlot(void **slots, size_t slot, void *newValue)
{
  DWORD oldProtect = 0;
  if(!VirtualProtect(&slots[slot], sizeof(void *), PAGE_READWRITE, &oldProtect))
    return false;

  slots[slot] = newValue;

  VirtualProtect(&slots[slot], sizeof(void *), oldProtect, &oldProtect);
  return true;
}

// Creates (or reuses) a private copy of obj's vtable and redirects the object
// to it. The copy covers the whole page containing the vtable, so it is always
// large enough for any COM interface.
CloneInfo *CreateClone(void *obj)
{
  auto it = s_Clones.find(obj);
  if(it != s_Clones.end())
  {
    it->second.refs++;
    return &it->second;
  }

  void *vtbl = *(void **)obj;
  if(vtbl == NULL)
    return NULL;

  void *pageBase = GetPageBase(vtbl);

  void *clonePage = VirtualAlloc(NULL, kClonePageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if(clonePage == NULL)
    return NULL;

  memcpy(clonePage, pageBase, kClonePageSize);

  DWORD oldProtect = 0;
  if(!VirtualProtect(clonePage, kClonePageSize, PAGE_EXECUTE_READWRITE, &oldProtect))
  {
    VirtualFree(clonePage, 0, MEM_RELEASE);
    return NULL;
  }

  void *cloneVtbl = (byte *)clonePage + ((uintptr_t)vtbl - (uintptr_t)pageBase);

  // COM objects are heap allocated, so their vtable pointer is writable.
  *(void **)obj = cloneVtbl;

  CloneInfo info;
  info.origVtbl = vtbl;
  info.clonePage = clonePage;
  info.refs = 1;
  s_Clones[obj] = info;

  return &s_Clones[obj];
}

void ReleaseClone(void *obj)
{
  auto it = s_Clones.find(obj);
  if(it == s_Clones.end())
    return;

  CloneInfo &info = it->second;
  if(info.refs > 1)
  {
    info.refs--;
    return;
  }

  *(void **)obj = info.origVtbl;
  VirtualFree(info.clonePage, 0, MEM_RELEASE);
  s_Clones.erase(it);
}
}    // namespace

void *GetSlot(void *obj, size_t slot)
{
  if(obj == NULL)
    return NULL;

  void *vtbl = *(void **)obj;
  if(vtbl == NULL)
    return NULL;

  return ((void **)vtbl)[slot];
}

bool Install(void *obj, size_t slot, void *detour, void **orig, bool cloneVtable)
{
  if(obj == NULL || detour == NULL)
    return false;

  SCOPED_LOCK(s_Lock);

  void *vtbl = *(void **)obj;
  if(vtbl == NULL)
    return false;

  void **slots = (void **)vtbl;
  void *oldValue = slots[slot];

  // Already hooked (by us or someone else installing the same detour).
  if(oldValue == detour)
    return false;

  if(cloneVtable)
  {
    if(CreateClone(obj) == NULL)
    {
      RDCERR("Failed to clone vtable for object %p", obj);
      return false;
    }

    // the object's vtable pointer may have been redirected by the clone
    vtbl = *(void **)obj;
    slots = (void **)vtbl;
    oldValue = slots[slot];
  }

  if(!PatchSlot(slots, slot, detour))
  {
    RDCERR("Failed to make vtable slot writable for object %p slot %u", obj, (uint32_t)slot);

    if(cloneVtable)
      ReleaseClone(obj);

    return false;
  }

  if(orig)
    *orig = oldValue;

  RDCDEBUG("Installed vtable hook on %p slot %u (orig %p)", obj, (uint32_t)slot, oldValue);
  return true;
}

bool Remove(void *obj, size_t slot, void *orig)
{
  if(obj == NULL)
    return false;

  SCOPED_LOCK(s_Lock);

  void *vtbl = *(void **)obj;
  if(vtbl == NULL)
    return false;

  void **slots = (void **)vtbl;

  if(orig != NULL && slots[slot] == orig)
  {
    // already restored
    ReleaseClone(obj);
    return true;
  }

  if(!PatchSlot(slots, slot, orig))
  {
    RDCERR("Failed to make vtable slot writable for object %p slot %u", obj, (uint32_t)slot);
    return false;
  }

  ReleaseClone(obj);
  return true;
}

Scoped::Scoped() : m_Obj(NULL), m_Slot(0), m_Orig(NULL), m_Installed(false)
{
}

Scoped::~Scoped()
{
  Remove();
}

bool Scoped::Install(void *obj, size_t slot, void *detour, bool cloneVtable)
{
  if(m_Installed)
    return true;

  void *orig = NULL;
  if(!VTableHook::Install(obj, slot, detour, &orig, cloneVtable))
    return false;

  m_Obj = obj;
  m_Slot = slot;
  m_Orig = orig;
  m_Installed = true;
  return true;
}

bool Scoped::Remove()
{
  if(!m_Installed)
    return true;

  bool ok = VTableHook::Remove(m_Obj, m_Slot, m_Orig);
  m_Installed = false;
  return ok;
}
}    // namespace VTableHook
