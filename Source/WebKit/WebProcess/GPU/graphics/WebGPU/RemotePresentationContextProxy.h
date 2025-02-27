/*
 * Copyright (C) 2022-2023 Apple Inc. All rights reserved.
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

#if ENABLE(GPU_PROCESS)

#include "RemoteGPUProxy.h"
#include "WebGPUIdentifier.h"
#include <pal/graphics/WebGPU/WebGPUIntegralTypes.h>
#include <pal/graphics/WebGPU/WebGPUPresentationContext.h>

namespace WebKit::WebGPU {

class ConvertToBackingContext;
class RemoteTextureProxy;

class RemotePresentationContextProxy final : public PAL::WebGPU::PresentationContext {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static Ref<RemotePresentationContextProxy> create(RemoteGPUProxy& parent, ConvertToBackingContext& convertToBackingContext, WebGPUIdentifier identifier)
    {
        return adoptRef(*new RemotePresentationContextProxy(parent, convertToBackingContext, identifier));
    }

    virtual ~RemotePresentationContextProxy();

    RemoteGPUProxy& parent() { return m_parent; }
    RemoteGPUProxy& root() { return m_parent->root(); }

    void present();

private:
    friend class DowncastConvertToBackingContext;

    RemotePresentationContextProxy(RemoteGPUProxy&, ConvertToBackingContext&, WebGPUIdentifier);

    RemotePresentationContextProxy(const RemotePresentationContextProxy&) = delete;
    RemotePresentationContextProxy(RemotePresentationContextProxy&&) = delete;
    RemotePresentationContextProxy& operator=(const RemotePresentationContextProxy&) = delete;
    RemotePresentationContextProxy& operator=(RemotePresentationContextProxy&&) = delete;

    WebGPUIdentifier backing() const { return m_backing; }

    static inline constexpr Seconds defaultSendTimeout = 30_s;
    template<typename T>
    WARN_UNUSED_RETURN bool send(T&& message)
    {
        return root().streamClientConnection().send(WTFMove(message), backing(), defaultSendTimeout);
    }
    template<typename T>
    WARN_UNUSED_RETURN IPC::Connection::SendSyncResult<T> sendSync(T&& message)
    {
        return root().streamClientConnection().sendSync(WTFMove(message), backing(), defaultSendTimeout);
    }

    void configure(const PAL::WebGPU::CanvasConfiguration&) final;
    void unconfigure() final;

    RefPtr<PAL::WebGPU::Texture> getCurrentTexture() final;

    WebGPUIdentifier m_backing;
    Ref<ConvertToBackingContext> m_convertToBackingContext;
    Ref<RemoteGPUProxy> m_parent;
    RefPtr<RemoteTextureProxy> m_currentTexture;
};

} // namespace WebKit::WebGPU

#endif // ENABLE(GPU_PROCESS)
