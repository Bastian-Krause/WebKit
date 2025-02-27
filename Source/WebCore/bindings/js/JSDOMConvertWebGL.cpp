/*
 * Copyright (C) 2017-2023 Apple Inc. All rights reserved.
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
#include "JSDOMConvertWebGL.h"

#if ENABLE(WEBGL)

#include "JSANGLEInstancedArrays.h"
#include "JSDOMConvertBufferSource.h"
#include "JSEXTBlendMinMax.h"
#include "JSEXTColorBufferFloat.h"
#include "JSEXTColorBufferHalfFloat.h"
#include "JSEXTDisjointTimerQuery.h"
#include "JSEXTDisjointTimerQueryWebGL2.h"
#include "JSEXTFloatBlend.h"
#include "JSEXTFragDepth.h"
#include "JSEXTPolygonOffsetClamp.h"
#include "JSEXTShaderTextureLOD.h"
#include "JSEXTTextureCompressionBPTC.h"
#include "JSEXTTextureCompressionRGTC.h"
#include "JSEXTTextureFilterAnisotropic.h"
#include "JSEXTTextureNorm16.h"
#include "JSEXTsRGB.h"
#include "JSKHRParallelShaderCompile.h"
#include "JSOESDrawBuffersIndexed.h"
#include "JSOESElementIndexUint.h"
#include "JSOESFBORenderMipmap.h"
#include "JSOESStandardDerivatives.h"
#include "JSOESTextureFloat.h"
#include "JSOESTextureFloatLinear.h"
#include "JSOESTextureHalfFloat.h"
#include "JSOESTextureHalfFloatLinear.h"
#include "JSOESVertexArrayObject.h"
#include "JSWebGLBuffer.h"
#include "JSWebGLClipCullDistance.h"
#include "JSWebGLColorBufferFloat.h"
#include "JSWebGLCompressedTextureASTC.h"
#include "JSWebGLCompressedTextureETC.h"
#include "JSWebGLCompressedTextureETC1.h"
#include "JSWebGLCompressedTexturePVRTC.h"
#include "JSWebGLCompressedTextureS3TC.h"
#include "JSWebGLCompressedTextureS3TCsRGB.h"
#include "JSWebGLDebugRendererInfo.h"
#include "JSWebGLDebugShaders.h"
#include "JSWebGLDepthTexture.h"
#include "JSWebGLDrawBuffers.h"
#include "JSWebGLDrawInstancedBaseVertexBaseInstance.h"
#include "JSWebGLFramebuffer.h"
#include "JSWebGLLoseContext.h"
#include "JSWebGLMultiDraw.h"
#include "JSWebGLMultiDrawInstancedBaseVertexBaseInstance.h"
#include "JSWebGLProgram.h"
#include "JSWebGLProvokingVertex.h"
#include "JSWebGLQuery.h"
#include "JSWebGLRenderbuffer.h"
#include "JSWebGLSampler.h"
#include "JSWebGLTexture.h"
#include "JSWebGLTimerQueryEXT.h"
#include "JSWebGLTransformFeedback.h"
#include "JSWebGLVertexArrayObject.h"
#include "JSWebGLVertexArrayObjectOES.h"
#include <JavaScriptCore/JSCInlines.h>

namespace WebCore {
using namespace JSC;

// FIXME: This should use the IDLUnion JSConverter.
JSValue convertToJSValue(JSGlobalObject& lexicalGlobalObject, JSDOMGlobalObject& globalObject, const WebGLAny& any)
{
    return WTF::switchOn(any,
        [] (std::nullptr_t) -> JSValue {
            return jsNull();
        }, [] (bool value) -> JSValue {
            return jsBoolean(value);
        }, [] (int value) -> JSValue {
            return jsNumber(value);
        }, [] (unsigned value) -> JSValue {
            return jsNumber(value);
        }, [] (long long value) -> JSValue {
            return jsNumber(value);
        }, [] (unsigned long long value) -> JSValue {
            return jsNumber(value);
        }, [] (float value) -> JSValue {
            return jsNumber(purifyNaN(value));
        }, [&] (const String& value) -> JSValue {
            return jsStringWithCache(lexicalGlobalObject.vm(), value);
        }, [&] (const Vector<bool>& values) -> JSValue {
            MarkedArgumentBuffer list;
            for (auto& value : values)
                list.append(jsBoolean(value));
            RELEASE_ASSERT(!list.hasOverflowed());
            return constructArray(&globalObject, static_cast<JSC::ArrayAllocationProfile*>(nullptr), list);
        }, [&] (const Vector<int>& values) -> JSValue {
            MarkedArgumentBuffer list;
            for (auto& value : values)
                list.append(jsNumber(value));
            RELEASE_ASSERT(!list.hasOverflowed());
            return constructArray(&globalObject, static_cast<JSC::ArrayAllocationProfile*>(nullptr), list);
        }, [&] (const Vector<unsigned>& values) -> JSValue {
            MarkedArgumentBuffer list;
            for (auto& value : values)
                list.append(jsNumber(value));
            RELEASE_ASSERT(!list.hasOverflowed());
            return constructArray(&globalObject, static_cast<JSC::ArrayAllocationProfile*>(nullptr), list);
        }, [&] (const RefPtr<Float32Array>& array) {
            return toJS(&lexicalGlobalObject, &globalObject, array.get());
        },
        [&] (const RefPtr<Int32Array>& array) {
            return toJS(&lexicalGlobalObject, &globalObject, array.get());
        },
        [&] (const RefPtr<Uint8Array>& array) {
            return toJS(&lexicalGlobalObject, &globalObject, array.get());
        },
        [&] (const RefPtr<Uint32Array>& array) {
            return toJS(&lexicalGlobalObject, &globalObject, array.get());
        },
        [&] (const RefPtr<WebGLBuffer>& buffer) {
            return toJS(&lexicalGlobalObject, &globalObject, buffer.get());
        },
        [&] (const RefPtr<WebGLFramebuffer>& buffer) {
            return toJS(&lexicalGlobalObject, &globalObject, buffer.get());
        },
        [&] (const RefPtr<WebGLProgram>& program) {
            return toJS(&lexicalGlobalObject, &globalObject, program.get());
        },
        [&] (const RefPtr<WebGLRenderbuffer>& buffer) {
            return toJS(&lexicalGlobalObject, &globalObject, buffer.get());
        },
        [&] (const RefPtr<WebGLTexture>& texture) {
            return toJS(&lexicalGlobalObject, &globalObject, texture.get());
        },
        [&] (const RefPtr<WebGLTimerQueryEXT>& query) {
            return toJS(&lexicalGlobalObject, &globalObject, query.get());
        },
        [&] (const RefPtr<WebGLVertexArrayObjectOES>& array) {
            return toJS(&lexicalGlobalObject, &globalObject, array.get());
        },
        [&] (const RefPtr<WebGLQuery>& query) {
            return toJS(&lexicalGlobalObject, &globalObject, query.get());
        },
        [&] (const RefPtr<WebGLSampler>& sampler) {
            return toJS(&lexicalGlobalObject, &globalObject, sampler.get());
        },
        [&] (const RefPtr<WebGLTransformFeedback>& transformFeedback) {
            return toJS(&lexicalGlobalObject, &globalObject, transformFeedback.get());
        },
        [&] (const RefPtr<WebGLVertexArrayObject>& array) {
            return toJS(&lexicalGlobalObject, &globalObject, array.get());
        }
    );
}

JSValue convertToJSValue(JSGlobalObject& lexicalGlobalObject, JSDOMGlobalObject& globalObject, WebGLExtension& extension)
{
#define TO_JS(EXT) \
    case WebGLExtension::EXT ## Name: \
        return toJS(&lexicalGlobalObject, &globalObject, static_cast<EXT&>(extension));

    switch (extension.getName()) {
        TO_JS(ANGLEInstancedArrays)
        TO_JS(EXTBlendMinMax)
        TO_JS(EXTColorBufferFloat)
        TO_JS(EXTColorBufferHalfFloat)
        TO_JS(EXTDisjointTimerQuery)
        TO_JS(EXTDisjointTimerQueryWebGL2)
        TO_JS(EXTFloatBlend)
        TO_JS(EXTFragDepth)
        TO_JS(EXTPolygonOffsetClamp)
        TO_JS(EXTShaderTextureLOD)
        TO_JS(EXTTextureCompressionBPTC)
        TO_JS(EXTTextureCompressionRGTC)
        TO_JS(EXTTextureFilterAnisotropic)
        TO_JS(EXTTextureNorm16)
        TO_JS(EXTsRGB)
        TO_JS(KHRParallelShaderCompile)
        TO_JS(OESDrawBuffersIndexed)
        TO_JS(OESElementIndexUint)
        TO_JS(OESFBORenderMipmap)
        TO_JS(OESStandardDerivatives)
        TO_JS(OESTextureFloat)
        TO_JS(OESTextureFloatLinear)
        TO_JS(OESTextureHalfFloat)
        TO_JS(OESTextureHalfFloatLinear)
        TO_JS(OESVertexArrayObject)
        TO_JS(WebGLClipCullDistance)
        TO_JS(WebGLColorBufferFloat)
        TO_JS(WebGLCompressedTextureASTC)
        TO_JS(WebGLCompressedTextureETC)
        TO_JS(WebGLCompressedTextureETC1)
        TO_JS(WebGLCompressedTexturePVRTC)
        TO_JS(WebGLCompressedTextureS3TC)
        TO_JS(WebGLCompressedTextureS3TCsRGB)
        TO_JS(WebGLDebugRendererInfo)
        TO_JS(WebGLDebugShaders)
        TO_JS(WebGLDepthTexture)
        TO_JS(WebGLDrawBuffers)
        TO_JS(WebGLDrawInstancedBaseVertexBaseInstance)
        TO_JS(WebGLLoseContext)
        TO_JS(WebGLMultiDraw)
        TO_JS(WebGLMultiDrawInstancedBaseVertexBaseInstance)
        TO_JS(WebGLProvokingVertex)
    }
    ASSERT_NOT_REACHED();
    return jsNull();
}

}

#endif

