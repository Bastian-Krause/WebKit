/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
#include "AccessibilityObjectInterface.h"
#include "IntRectHash.h"
#include <wtf/RefCounted.h>

namespace WebCore {

class AXObjectCache;

class AXGeometryManager : public ThreadSafeRefCounted<AXGeometryManager> {
    WTF_MAKE_NONCOPYABLE(AXGeometryManager);
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit AXGeometryManager(AXObjectCache&);
    AXGeometryManager();
    static Ref<AXGeometryManager> create(AXObjectCache& cache)
    {
        return adoptRef(*new AXGeometryManager(cache));
    }

    void willUpdateObjectRegions();
    void scheduleObjectRegionsUpdate(bool /* scheduleImmediately */);

    void onPaint(AXID, IntRect&&);
    // std::nullopt if there is no paint rect for the given ID (i.e. because it hasn't yet been painted, or cannot be painted at all).
    std::optional<IntRect> paintRectForID(AXID);

    void remove(AXID axID) { m_paintRects.remove(axID); }

#if PLATFORM(MAC)
    FloatRect primaryScreenRect() const;
#endif

private:
    void updateObjectRegionsTimerFired() { scheduleRenderingUpdate(); }
    void scheduleRenderingUpdate();

    // The cache that owns this instance.
    WeakPtr<AXObjectCache> m_cache;
    // Bounds computed by PaintPhase::Accessibility.
    HashMap<AXID, IntRect> m_paintRects;
    Timer m_updateObjectRegionsTimer;

#if PLATFORM(MAC)
    FloatRect m_primaryScreenRect;
#endif
};

} // namespace WebCore

#endif // ENABLE(ACCESSIBILITY_ISOLATED_TREE)
