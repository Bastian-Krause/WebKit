/*
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WebGPUBufferImpl.h"

#if HAVE(WEBGPU_IMPLEMENTATION)

#include "WebGPUConvertToBackingContext.h"
#include <WebGPU/WebGPUExt.h>
#include <wtf/BlockPtr.h>

namespace PAL::WebGPU {

BufferImpl::BufferImpl(WGPUBuffer buffer, ConvertToBackingContext& convertToBackingContext)
    : m_backing(buffer)
    , m_convertToBackingContext(convertToBackingContext)
{
}

BufferImpl::~BufferImpl()
{
    wgpuBufferRelease(m_backing);
}

static Size64 getMappedSize(WGPUBuffer buffer, std::optional<Size64> size, Size64 offset)
{
    if (size.has_value())
        return size.value();

    auto bufferSize = wgpuBufferGetSize(buffer);
    return bufferSize > offset ? (bufferSize - offset) : 0;
}

void BufferImpl::mapAsync(MapModeFlags mapModeFlags, Size64 offset, std::optional<Size64> size, CompletionHandler<void(bool)>&& callback)
{
    auto backingMapModeFlags = m_convertToBackingContext->convertMapModeFlagsToBacking(mapModeFlags);
    auto usedSize = getMappedSize(m_backing, size, offset);

    // FIXME: Check the casts.
    wgpuBufferMapAsyncWithBlock(m_backing, backingMapModeFlags, static_cast<size_t>(offset), static_cast<size_t>(usedSize), makeBlockPtr([callback = WTFMove(callback)](WGPUBufferMapAsyncStatus status) mutable {
        callback(status == WGPUBufferMapAsyncStatus_Success ? true : false);
    }).get());
}

auto BufferImpl::getMappedRange(Size64 offset, std::optional<Size64> size) -> MappedRange
{
    auto usedSize = getMappedSize(m_backing, size, offset);

    // FIXME: Check the casts.
    auto* pointer = wgpuBufferGetMappedRange(m_backing, static_cast<size_t>(offset), static_cast<size_t>(usedSize));
    // FIXME: Check the type narrowing.
    auto bufferSize = wgpuBufferGetSize(m_backing);
    size_t actualSize = pointer ? static_cast<size_t>(bufferSize) : 0;
    size_t actualOffset = pointer ? static_cast<size_t>(offset) : 0;
    return { static_cast<uint8_t*>(pointer) - actualOffset, actualSize };
}

void BufferImpl::unmap()
{
    wgpuBufferUnmap(m_backing);
}

void BufferImpl::destroy()
{
    wgpuBufferDestroy(m_backing);
}

void BufferImpl::setLabelInternal(const String& label)
{
    wgpuBufferSetLabel(m_backing, label.utf8().data());
}

} // namespace PAL::WebGPU

#endif // HAVE(WEBGPU_IMPLEMENTATION)
