/*
 * Copyright (C) 2017-2019 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WasmInstance.h"

#if ENABLE(WEBASSEMBLY)

#include "JSCJSValueInlines.h"
#include "JSWebAssemblyHelpers.h"
#include "JSWebAssemblyInstance.h"
#include "Register.h"
#include "WasmModuleInformation.h"
#include "WasmTag.h"
#include "WasmTypeDefinitionInlines.h"
#include <wtf/CheckedArithmetic.h>

namespace JSC { namespace Wasm {

Instance::Instance(VM& vm, JSGlobalObject* globalObject, Ref<Module>&& module)
    : m_vm(&vm)
    , m_globalObject(globalObject)
    , m_module(WTFMove(module))
    , m_globalsToMark(m_module.get().moduleInformation().globalCount())
    , m_globalsToBinding(m_module.get().moduleInformation().globalCount())
    , m_numImportFunctions(m_module->moduleInformation().importFunctionCount())
    , m_passiveElements(m_module->moduleInformation().elementCount())
    , m_passiveDataSegments(m_module->moduleInformation().dataSegmentsCount())
    , m_tags(m_module->moduleInformation().exceptionIndexSpaceSize())
{
    ASSERT(static_cast<ptrdiff_t>(Instance::offsetOfCachedMemory() + sizeof(void*)) == Instance::offsetOfCachedBoundsCheckingSize());
    for (unsigned i = 0; i < m_numImportFunctions; ++i)
        new (importFunctionInfo(i)) ImportFunctionInfo();
    m_globals = bitwise_cast<Global::Value*>(bitwise_cast<char*>(this) + offsetOfGlobalPtr(m_numImportFunctions, m_module->moduleInformation().tableCount(), 0));
    memset(bitwise_cast<char*>(m_globals), 0, m_module->moduleInformation().globalCount() * sizeof(Global::Value));
    for (unsigned i = 0; i < m_module->moduleInformation().globals.size(); ++i) {
        const Wasm::GlobalInformation& global = m_module.get().moduleInformation().globals[i];
        if (global.bindingMode == Wasm::GlobalInformation::BindingMode::Portable) {
            // This is kept alive by JSWebAssemblyInstance -> JSWebAssemblyGlobal -> binding.
            m_globalsToBinding.set(i);
        } else if (isRefType(global.type)) {
            // This is kept alive by JSWebAssemblyInstance -> binding.
            m_globalsToMark.set(i);
        }
    }
    memset(bitwise_cast<char*>(this) + offsetOfTablePtr(m_numImportFunctions, 0), 0, m_module->moduleInformation().tableCount() * sizeof(Table*));
    for (unsigned elementIndex = 0; elementIndex < m_module->moduleInformation().elementCount(); ++elementIndex) {
        const auto& element = m_module->moduleInformation().elements[elementIndex];
        if (element.isPassive())
            m_passiveElements.quickSet(elementIndex);
    }

    for (unsigned dataSegmentIndex = 0; dataSegmentIndex < m_module->moduleInformation().dataSegmentsCount(); ++dataSegmentIndex) {
        const auto& dataSegment = m_module->moduleInformation().data[dataSegmentIndex];
        if (dataSegment->isPassive())
            m_passiveDataSegments.quickSet(dataSegmentIndex);
    }
}

Ref<Instance> Instance::create(VM& vm, JSGlobalObject* globalObject, Ref<Module>&& module)
{
    ASSERT(allocationSize(maxImports, maxTables, maxGlobals) <= INT32_MAX);
    return adoptRef(*new (NotNull, fastMalloc(allocationSize(module->moduleInformation().importFunctionCount(), module->moduleInformation().tableCount(), module->moduleInformation().globalCount()))) Instance(vm, globalObject, WTFMove(module)));
}

Instance::~Instance() = default;

size_t Instance::extraMemoryAllocated() const
{
    return allocationSize(m_numImportFunctions, m_module->moduleInformation().tableCount(), m_module->moduleInformation().globalCount());
}

void Instance::setGlobal(unsigned i, JSValue value)
{
    Global::Value& slot = m_globals[i];
    if (m_globalsToBinding.get(i)) {
        Wasm::Global* global = getGlobalBinding(i);
        if (!global)
            return;
        global->valuePointer()->m_externref.set(vm(), global->owner(), value);
        return;
    }
    ASSERT(m_owner);
    slot.m_externref.set(vm(), owner(), value);
}

JSValue Instance::getFunctionWrapper(unsigned i) const
{
    JSValue value = m_functionWrappers.get(i).get();
    if (value.isEmpty())
        return jsNull();
    return value;
}

void Instance::setFunctionWrapper(unsigned i, JSValue value)
{
    ASSERT(m_owner);
    ASSERT(value.isCallable());
    ASSERT(!m_functionWrappers.contains(i));
    Locker locker { owner()->cellLock() };
    m_functionWrappers.set(i, WriteBarrier<Unknown>(vm(), owner(), value));
    ASSERT(getFunctionWrapper(i) == value);
}

Table* Instance::table(unsigned i)
{
    RELEASE_ASSERT(i < m_module->moduleInformation().tableCount());
    return *bitwise_cast<Table**>(bitwise_cast<char*>(this) + offsetOfTablePtr(m_numImportFunctions, i));
}

void Instance::tableCopy(uint32_t dstOffset, uint32_t srcOffset, uint32_t length, uint32_t dstTableIndex, uint32_t srcTableIndex)
{
    RELEASE_ASSERT(srcTableIndex < m_module->moduleInformation().tableCount());
    RELEASE_ASSERT(dstTableIndex < m_module->moduleInformation().tableCount());

    Table* dstTable = table(dstTableIndex);
    Table* srcTable = table(srcTableIndex);
    RELEASE_ASSERT(dstTable->type() == srcTable->type());

    auto forEachTableElement = [&](auto fn) {
        if (dstTableIndex == srcTableIndex && dstOffset > srcOffset) {
            for (uint32_t index = length; index--;)
                fn(dstTable, srcTable, dstOffset + index, srcOffset + index);
        } else if (dstTableIndex == srcTableIndex && dstOffset == srcOffset)
            return;
        else {
            for (uint32_t index = 0; index < length; ++index)
                fn(dstTable, srcTable, dstOffset + index, srcOffset + index);
        }
    };

    if (dstTable->isExternrefTable()) {
        forEachTableElement([](Table* dstTable, Table* srcTable, uint32_t dstIndex, uint32_t srcIndex) {
            dstTable->copy(srcTable, dstIndex, srcIndex);
        });
        return;
    }

    forEachTableElement([](Table* dstTable, Table* srcTable, uint32_t dstIndex, uint32_t srcIndex) {
        dstTable->asFuncrefTable()->copyFunction(srcTable->asFuncrefTable(), dstIndex, srcIndex);
    });
}

void Instance::elemDrop(uint32_t elementIndex)
{
    m_passiveElements.quickClear(elementIndex);
}

bool Instance::memoryInit(uint32_t dstAddress, uint32_t srcAddress, uint32_t length, uint32_t dataSegmentIndex)
{
    RELEASE_ASSERT(dataSegmentIndex < module().moduleInformation().dataSegmentsCount());

    if (sumOverflows<uint32_t>(srcAddress, length))
        return false;

    const Segment::Ptr& segment = module().moduleInformation().data[dataSegmentIndex];
    const uint32_t segmentSizeInBytes = m_passiveDataSegments.quickGet(dataSegmentIndex) ? segment->sizeInBytes : 0U;
    if (srcAddress + length > segmentSizeInBytes)
        return false;

    const uint8_t* segmentData = !length ? nullptr : &segment->byte(srcAddress);

    ASSERT(memory());
    return memory()->init(dstAddress, segmentData, length);
}

void Instance::dataDrop(uint32_t dataSegmentIndex)
{
    m_passiveDataSegments.quickClear(dataSegmentIndex);
}

const Element* Instance::elementAt(unsigned index) const
{
    RELEASE_ASSERT(index < m_module->moduleInformation().elementCount());

    if (m_passiveElements.quickGet(index))
        return &m_module->moduleInformation().elements[index];
    return nullptr;
}

void Instance::initElementSegment(uint32_t tableIndex, const Element& segment, uint32_t dstOffset, uint32_t srcOffset, uint32_t length)
{
    RELEASE_ASSERT(length <= segment.length());

    JSWebAssemblyInstance* jsInstance = owner();
    JSWebAssemblyTable* jsTable = jsInstance->table(tableIndex);
    JSGlobalObject* globalObject = jsInstance->globalObject();
    VM& vm = globalObject->vm();

    for (uint32_t index = 0; index < length; ++index) {
        const auto srcIndex = srcOffset + index;
        const auto dstIndex = dstOffset + index;

        if (Element::isNullFuncIndex(segment.functionIndices[srcIndex])) {
            jsTable->clear(dstIndex);
            continue;
        }

        // FIXME: This essentially means we're exporting an import.
        // We need a story here. We need to create a WebAssemblyFunction
        // for the import.
        // https://bugs.webkit.org/show_bug.cgi?id=165510
        uint32_t functionIndex = segment.functionIndices[srcIndex];
        TypeIndex typeIndex = m_module->typeIndexFromFunctionIndexSpace(functionIndex);
        if (isImportFunction(functionIndex)) {
            JSObject* functionImport = importFunction(functionIndex).get();
            if (isWebAssemblyHostFunction(functionImport)) {
                WebAssemblyFunction* wasmFunction = jsDynamicCast<WebAssemblyFunction*>(functionImport);
                // If we ever import a WebAssemblyWrapperFunction, we set the import as the unwrapped value.
                // Because a WebAssemblyWrapperFunction can never wrap another WebAssemblyWrapperFunction,
                // the only type this could be is WebAssemblyFunction.
                RELEASE_ASSERT(wasmFunction);
                jsTable->set(dstIndex, wasmFunction);
                continue;
            }
            auto* wrapperFunction = WebAssemblyWrapperFunction::create(
                vm,
                globalObject,
                globalObject->webAssemblyWrapperFunctionStructure(),
                functionImport,
                functionIndex,
                jsInstance,
                typeIndex,
                TypeInformation::getCanonicalRTT(typeIndex));
            jsTable->set(dstIndex, wrapperFunction);
            continue;
        }

        Callee& jsEntrypointCallee = calleeGroup()->jsEntrypointCalleeFromFunctionIndexSpace(functionIndex);
        WasmToWasmImportableFunction::LoadLocation entrypointLoadLocation = calleeGroup()->entrypointLoadLocationFromFunctionIndexSpace(functionIndex);
        const auto& signature = TypeInformation::getFunctionSignature(typeIndex);
        // FIXME: Say we export local function "foo" at function index 0.
        // What if we also set it to the table an Element w/ index 0.
        // Does (new Instance(...)).exports.foo === table.get(0)?
        // https://bugs.webkit.org/show_bug.cgi?id=165825
        WebAssemblyFunction* function = WebAssemblyFunction::create(
            vm,
            globalObject,
            globalObject->webAssemblyFunctionStructure(),
            signature.argumentCount(),
            WTF::makeString(functionIndex),
            jsInstance,
            jsEntrypointCallee,
            entrypointLoadLocation,
            typeIndex,
            TypeInformation::getCanonicalRTT(typeIndex));
        jsTable->set(dstIndex, function);
    }
}

template<typename T>
bool Instance::copyDataSegment(uint32_t segmentIndex, uint32_t offset, uint32_t lengthInBytes, FixedVector<T>& values)
{
    // Fail if the data segment index is out of bounds
    RELEASE_ASSERT(segmentIndex < module().moduleInformation().dataSegmentsCount());
    // Otherwise, get the `segmentIndex`th data segment
    const Segment::Ptr& segment = module().moduleInformation().data[segmentIndex];
    const uint32_t segmentSizeInBytes = m_passiveDataSegments.quickGet(segmentIndex) ? segment->sizeInBytes : 0U;

    // Caller checks that the (offset + lengthInBytes) calculation doesn't overflow
    if ((offset + lengthInBytes) > segmentSizeInBytes) {
        // The segment access would overflow; the caller must handle this error.
        return false;
    }
    // If size is 0, do nothing
    if (!lengthInBytes)
        return true;
    // Cast the data segment to a pointer
    const uint8_t* segmentData = &segment->byte(offset);

    // Copy the data from the segment into the out param vector
    memcpy(reinterpret_cast<uint8_t*>(values.data()), segmentData, lengthInBytes);

    return true;
}

template bool Instance::copyDataSegment<uint8_t>(uint32_t, uint32_t, uint32_t, FixedVector<uint8_t>&);
template bool Instance::copyDataSegment<uint16_t>(uint32_t, uint32_t, uint32_t, FixedVector<uint16_t>&);
template bool Instance::copyDataSegment<uint32_t>(uint32_t, uint32_t, uint32_t, FixedVector<uint32_t>&);
template bool Instance::copyDataSegment<uint64_t>(uint32_t, uint32_t, uint32_t, FixedVector<uint64_t>&);


void Instance::copyElementSegment(const Element& segment, uint32_t srcOffset, uint32_t length, FixedVector<uint64_t>& values)
{
    // Caller should have already checked that the (offset + length) calculation doesn't overflow int32,
    // and that the (offset + length) doesn't overflow the element segment
    ASSERT(!sumOverflows<uint32_t>(srcOffset, length));
    ASSERT((srcOffset + length) <= segment.length());

    for (uint32_t srcIndex = srcOffset; srcIndex < length; ++srcIndex) {
        const auto dstIndex = srcIndex - srcOffset;

        // Represent the null function as the null JS value
        if (Element::isNullFuncIndex(segment.functionIndices[srcIndex])) {
            values[dstIndex] = static_cast<uint64_t>(JSValue::encode(jsNull()));
            continue;
        }

        // FIXME
        // This will have to be updated to handle element types other than function references
        // when https://bugs.webkit.org/show_bug.cgi?id=251874 is fixed
        uint32_t functionIndex = segment.functionIndices[srcIndex];

        // A wrapper for this function should have been created during parsing.
        // A future optimization would be for the parser to not create the wrappers,
        // and create them here dynamically instead.
        JSValue value = getFunctionWrapper(functionIndex);
        ASSERT(value.isCallable());
        values[dstIndex] = static_cast<uint64_t>(JSValue::encode(value));
    }
}

void Instance::tableInit(uint32_t dstOffset, uint32_t srcOffset, uint32_t length, uint32_t elementIndex, uint32_t tableIndex)
{
    RELEASE_ASSERT(elementIndex < m_module->moduleInformation().elementCount());
    RELEASE_ASSERT(tableIndex < m_module->moduleInformation().tableCount());

    const Element* elementSegment = elementAt(elementIndex);
    RELEASE_ASSERT(elementSegment);
    RELEASE_ASSERT(elementSegment->isPassive());
    initElementSegment(tableIndex, *elementSegment, dstOffset, srcOffset, length);
}

void Instance::setTable(unsigned i, Ref<Table>&& table)
{
    RELEASE_ASSERT(i < m_module->moduleInformation().tableCount());
    ASSERT(!this->table(i));
    *bitwise_cast<Table**>(bitwise_cast<char*>(this) + offsetOfTablePtr(m_numImportFunctions, i)) = &table.leakRef();
}

void Instance::linkGlobal(unsigned i, Ref<Global>&& global)
{
    m_globals[i].m_pointer = global->valuePointer();
    m_linkedGlobals.set(i, WTFMove(global));
}

void Instance::setTag(unsigned index, Ref<const Tag>&& tag)
{
    m_tags[index] = WTFMove(tag);
}

} } // namespace JSC::Wasm

#endif // ENABLE(WEBASSEMBLY)
