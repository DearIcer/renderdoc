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

#include "hook_impl.h"
#include "common/threading.h"

#ifdef USE_MINHOOK

static bool s_Initialized = false;
static Threading::CriticalSection s_Lock;
static rdcarray<HookImpl::HookContext> s_Hooks;

bool HookImpl::Initialize()
{
  SCOPED_LOCK(s_Lock);
  if(s_Initialized)
    return true;

  MH_STATUS status = MH_Initialize();
  if(status != MH_OK)
  {
    RDCERR("MinHook initialization failed: %s", MH_StatusToString(status));
    return false;
  }

  s_Initialized = true;
  return true;
}

void HookImpl::Uninitialize()
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return;

  MH_Uninitialize();
  s_Hooks.clear();
  s_Initialized = false;
}

bool HookImpl::CreateHook(void *target, void *detour, void **original)
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
  {
    RDCERR("HookImpl not initialized");
    return false;
  }

  MH_STATUS status = MH_CreateHook(target, detour, original);
  if(status != MH_OK)
  {
    RDCERR("MinHook CreateHook failed: %s", MH_StatusToString(status));
    return false;
  }

  HookImpl::HookContext ctx = {target, detour, original ? *original : NULL, false};
  s_Hooks.push_back(ctx);
  return true;
}

bool HookImpl::EnableHook(void *target)
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return false;

  MH_STATUS status = MH_EnableHook(target);
  if(status != MH_OK)
  {
    RDCERR("MinHook EnableHook failed: %s", MH_StatusToString(status));
    return false;
  }

  for(HookImpl::HookContext &ctx : s_Hooks)
  {
    if(ctx.target == target)
    {
      ctx.enabled = true;
      break;
    }
  }
  return true;
}

bool HookImpl::DisableHook(void *target)
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return false;

  MH_STATUS status = MH_DisableHook(target);
  if(status != MH_OK)
  {
    RDCERR("MinHook DisableHook failed: %s", MH_StatusToString(status));
    return false;
  }

  for(HookImpl::HookContext &ctx : s_Hooks)
  {
    if(ctx.target == target)
    {
      ctx.enabled = false;
      break;
    }
  }
  return true;
}

bool HookImpl::RemoveHook(void *target)
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return false;

  MH_STATUS status = MH_RemoveHook(target);
  if(status != MH_OK)
  {
    RDCERR("MinHook RemoveHook failed: %s", MH_StatusToString(status));
    return false;
  }

  for(size_t i = 0; i < s_Hooks.size(); i++)
  {
    if(s_Hooks[i].target == target)
    {
      s_Hooks.erase(i);
      break;
    }
  }
  return true;
}

bool HookImpl::EnableAllHooks()
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return false;

  MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
  if(status != MH_OK)
  {
    RDCERR("MinHook EnableAllHooks failed: %s", MH_StatusToString(status));
    return false;
  }

  for(HookImpl::HookContext &ctx : s_Hooks)
    ctx.enabled = true;
  return true;
}

bool HookImpl::DisableAllHooks()
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return false;

  MH_STATUS status = MH_DisableHook(MH_ALL_HOOKS);
  if(status != MH_OK)
  {
    RDCERR("MinHook DisableAllHooks failed: %s", MH_StatusToString(status));
    return false;
  }

  for(HookImpl::HookContext &ctx : s_Hooks)
    ctx.enabled = false;
  return true;
}

#else

static bool s_Initialized = false;
static Threading::CriticalSection s_Lock;
static rdcarray<HookImpl::HookContext> s_Hooks;

bool HookImpl::Initialize()
{
  SCOPED_LOCK(s_Lock);
  s_Initialized = true;
  return true;
}

void HookImpl::Uninitialize()
{
  SCOPED_LOCK(s_Lock);
  s_Hooks.clear();
  s_Initialized = false;
}

bool HookImpl::CreateHook(void *target, void *detour, void **original)
{
  SCOPED_LOCK(s_Lock);
  if(!s_Initialized)
    return false;

  HookImpl::HookContext ctx = {target, detour, original ? *original : NULL, false};
  s_Hooks.push_back(ctx);
  return true;
}

bool HookImpl::EnableHook(void *target)
{
  SCOPED_LOCK(s_Lock);
  for(HookImpl::HookContext &ctx : s_Hooks)
  {
    if(ctx.target == target)
    {
      ctx.enabled = true;
      return true;
    }
  }
  return false;
}

bool HookImpl::DisableHook(void *target)
{
  SCOPED_LOCK(s_Lock);
  for(HookImpl::HookContext &ctx : s_Hooks)
  {
    if(ctx.target == target)
    {
      ctx.enabled = false;
      return true;
    }
  }
  return false;
}

bool HookImpl::RemoveHook(void *target)
{
  SCOPED_LOCK(s_Lock);
  for(size_t i = 0; i < s_Hooks.size(); i++)
  {
    if(s_Hooks[i].target == target)
    {
      s_Hooks.erase(i);
      return true;
    }
  }
  return false;
}

bool HookImpl::EnableAllHooks()
{
  SCOPED_LOCK(s_Lock);
  for(HookImpl::HookContext &ctx : s_Hooks)
    ctx.enabled = true;
  return true;
}

bool HookImpl::DisableAllHooks()
{
  SCOPED_LOCK(s_Lock);
  for(HookImpl::HookContext &ctx : s_Hooks)
    ctx.enabled = false;
  return true;
}

#endif
