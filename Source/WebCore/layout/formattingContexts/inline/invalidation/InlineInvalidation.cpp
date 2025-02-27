/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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
#include "InlineInvalidation.h"

#include "InlineDamage.h"
#include "InlineFormattingState.h"
#include "InlineSoftLineBreakItem.h"
#include "LayoutUnit.h"
#include "TextDirection.h"
#include <wtf/Range.h>

namespace WebCore {
namespace Layout {

InlineInvalidation::InlineInvalidation(InlineDamage& inlineDamage, const InlineItems& inlineItems, const InlineDisplay::Content& displayContent)
    : m_inlineDamage(inlineDamage)
    , m_inlineItems(inlineItems)
    , m_displayContent(displayContent)
{
}

void InlineInvalidation::styleChanged(const Box& layoutBox, const RenderStyle& oldStyle)
{
    UNUSED_PARAM(layoutBox);
    UNUSED_PARAM(oldStyle);

    m_inlineDamage.setDamageType(InlineDamage::Type::NeedsContentUpdateAndLineLayout);
}

struct DamagedContent {
    const Box& layoutBox;
    // Only text type of boxes may have offset. No offset also simply points to the end of the layout box.
    std::optional<size_t> offset { };
    enum class Type : uint8_t {
        Insertion,
        Removal
    };
    Type type { Type::Insertion };
};
static std::optional<size_t> damagedLineIndex(std::optional<DamagedContent> damagedContent, const InlineDisplay::Boxes& displayBoxes)
{
    ASSERT(!displayBoxes.isEmpty());

    if (!damagedContent || displayBoxes.size() == 1) {
        // Let's return the first line on empty content (when only root inline display box is present). 
        return displayBoxes.last().lineIndex();
    }

    auto lastDisplayBoxIndexForLayoutBox = [&]() -> std::optional<size_t> {
        for (auto index = displayBoxes.size(); index--;) {
            auto& displayBox = displayBoxes[index];
            if (&damagedContent->layoutBox == &displayBox.layoutBox())
                return index;
        }
        // FIXME: When the damaged layout box does not generate even one display box (all collapsed), we should
        // look at previous/next content to damage lines.
        return { };
    };
    auto lastDisplayBoxIndex = lastDisplayBoxIndexForLayoutBox();
    if (!lastDisplayBoxIndex) {
        // FIXME: Completely collapsed content should not trigger full invalidation.
        return { };
    }

    auto candidateLineIndex = [&](auto damagedDisplayBoxIndex) -> std::optional<size_t> {
        if (!damagedDisplayBoxIndex || damagedDisplayBoxIndex >= displayBoxes.size()) {
            ASSERT_NOT_REACHED();
            return { };
        }
        auto& damagedDisplayBox = displayBoxes[damagedDisplayBoxIndex];
        ASSERT(&damagedDisplayBox.layoutBox() == &damagedContent->layoutBox);
        // In case of content deletion, we may need to damage the "previous" line.
        if (damagedContent->type == DamagedContent::Type::Insertion || !damagedDisplayBox.lineIndex())
            return { damagedDisplayBox.lineIndex() };
        if (!displayBoxes[damagedDisplayBoxIndex - 1].isRootInlineBox()) {
            // There's more content in front, it's safe to stay on the current line.
            ASSERT(displayBoxes[damagedDisplayBoxIndex - 1].lineIndex() == damagedDisplayBox.lineIndex());
            return { damagedDisplayBox.lineIndex() };
        }
        auto damagedContentIsFirstContentOnLine = !damagedDisplayBox.isText() || (damagedContent->offset && (!*damagedContent->offset || damagedDisplayBox.text().start() == *damagedContent->offset));
        return { damagedContentIsFirstContentOnLine ? damagedDisplayBox.lineIndex() - 1 : damagedDisplayBox.lineIndex() };
    };

    auto leadingIndexForDisplayBox = *lastDisplayBoxIndex;
    if (damagedContent->layoutBox.isLineBreakBox()) {
        auto& displayBox = displayBoxes[leadingIndexForDisplayBox];
        return displayBox.lineIndex() ? displayBox.lineIndex() - 1 : 0;
    }

    if (is<InlineTextBox>(damagedContent->layoutBox)) {
        if (!damagedContent->offset)
            return candidateLineIndex(leadingIndexForDisplayBox);

        for (auto index = leadingIndexForDisplayBox; index > 0; --index) {
            auto& displayBox = displayBoxes[index];
            if (displayBox.isRootInlineBox() || (displayBox.isInlineBox() && !displayBox.isFirstForLayoutBox())) {
                // Multi line damage includes line leading root inline display boxes (and maybe line spanning inline boxes).
                continue;
            }
            if (&displayBoxes[index].layoutBox() != &damagedContent->layoutBox) {
                // We should always be able to find the damaged offset within this layout box.
                break;
            }
            ASSERT(displayBox.isTextOrSoftLineBreak());
            // Find out which display box has the damaged offset position.
            auto startOffset = displayBox.text().start();
            if (startOffset <= *damagedContent->offset) {
                // FIXME: Add support for leading inline box deletion too.
                return candidateLineIndex(index);
            }
            leadingIndexForDisplayBox = index;
        }
        // In some cases leading content does not produce a display box (collapsed whitespace), let's just use the first display box.
        ASSERT(leadingIndexForDisplayBox);
        return candidateLineIndex(leadingIndexForDisplayBox);
    }
    ASSERT_NOT_IMPLEMENTED_YET();
    return { };
}

static const InlineDisplay::Box* leadingContentDisplayForLineIndex(size_t lineIndex, const InlineDisplay::Boxes& displayBoxes)
{
    auto rootInlineBoxIndexOnLine = [&]() -> size_t {
        for (auto index = displayBoxes.size(); index--;) {
            auto& displayBox = displayBoxes[index];
            if (displayBox.lineIndex() == lineIndex && displayBox.isRootInlineBox())
                return index;
        }
        ASSERT_NOT_REACHED();
        return 0;
    };

    for (auto firstContentDisplayBoxIndex = rootInlineBoxIndexOnLine() + 1; firstContentDisplayBoxIndex < displayBoxes.size(); ++firstContentDisplayBoxIndex) {
        auto& displayBox = displayBoxes[firstContentDisplayBoxIndex];
        ASSERT(!displayBox.isRootInlineBox());
        if (!displayBox.isNonRootInlineBox() || displayBox.isFirstForLayoutBox())
            return &displayBox;
    }
    return nullptr;
}

static std::optional<InlineItemPosition> inlineItemPositionForDisplayBox(const InlineDisplay::Box& displayBox, const InlineItems& inlineItems)
{
    ASSERT(!inlineItems.isEmpty());

    auto lastInlineItemIndexForDisplayBox = [&]() -> std::optional<size_t> {
        for (size_t index = inlineItems.size(); index--;) {
            if (&inlineItems[index].layoutBox() == &displayBox.layoutBox() && !inlineItems[index].isInlineBoxEnd())
                return index;
        }
        return { };
    }();

    if (!lastInlineItemIndexForDisplayBox)
        return { };

    // This is our last InlineItem associated with this layout box. Let's find out
    // which previous InlineItem is at the beginning of this display box.
    auto inlineItemIndex = *lastInlineItemIndexForDisplayBox;
    if (!displayBox.isTextOrSoftLineBreak())
        return InlineItemPosition { inlineItemIndex, 0 };
    // 1. A display box could consist of one or more InlineItems.
    // 2. A display box could start at any offset inside an InlineItem.
    auto startOffset = displayBox.text().start();
    while (true) {
        auto inlineTextItemRange = [&]() -> WTF::Range<unsigned> {
            if (is<InlineTextItem>(inlineItems[inlineItemIndex])) {
                auto& inlineTextItem = downcast<InlineTextItem>(inlineItems[inlineItemIndex]);
                return { inlineTextItem.start(), inlineTextItem.end() };
            }
            if (is<InlineSoftLineBreakItem>(inlineItems[inlineItemIndex])) {
                auto startPosition = downcast<InlineSoftLineBreakItem>(inlineItems[inlineItemIndex]).position();
                return { startPosition, startPosition + 1 };
            }
            ASSERT_NOT_REACHED();
            return { };
        };
        auto textRange = inlineTextItemRange();
        if (textRange.begin() <= startOffset && textRange.end() > startOffset)
            return InlineItemPosition { inlineItemIndex, startOffset - textRange.begin() };
        if (!inlineItemIndex--)
            break;
    }
    return { };
}

static std::optional<InlineItemPosition> inlineItemPositionForDamagedContentPosition(const DamagedContent& damagedContent, InlineItemPosition candidatePosition, const InlineItems& inlineItems)
{
    ASSERT(candidatePosition.index < inlineItems.size());
    if (!candidatePosition)
        return candidatePosition;
    if (!is<InlineTextBox>(damagedContent.layoutBox)) {
        ASSERT(!damagedContent.offset);
        return candidatePosition;
    }
    auto candidateInlineItem = inlineItems[candidatePosition.index];
    if (&candidateInlineItem.layoutBox() != &damagedContent.layoutBox || !is<InlineTextItem>(candidateInlineItem))
        return candidatePosition;
    auto& inlineTextItem = downcast<InlineTextItem>(candidateInlineItem);
    if (!damagedContent.offset) {
        // When damage points to "after" the layout box, whatever InlineItem we found is surely before the damage.
        return candidatePosition;
    }
    if (inlineTextItem.start() + candidatePosition.offset <= *damagedContent.offset)
        return candidatePosition;
    // The damage offset is in front of the first display box we managed to find for this layout box.
    // Let's adjust the candidate position by moving it over to the damaged offset.
    for (auto index = candidatePosition.index; index--;) {
        if (&candidateInlineItem.layoutBox() != &inlineItems[index].layoutBox()) {
            // We should stay on the same layout box.
            ASSERT_NOT_REACHED();
            return { };
        }
        auto& previousInlineItem = downcast<InlineTextItem>(inlineItems[index]);
        if (previousInlineItem.start() <= *damagedContent.offset)
            return InlineItemPosition { index, { } };
    }
    return { };
}

struct DamagedLine {
    size_t index { 0 };
    InlineItemPosition leadingInlineItemPosition { };
};
static std::optional<DamagedLine> leadingInlineItemPositionForDamage(std::optional<DamagedContent> damagedContent, const InlineItems& inlineItems, const InlineDisplay::Boxes& displayBoxes)
{
    ASSERT(!displayBoxes.isEmpty());
    // 1. Find the root inline box based on the damaged layout box (this is our damaged line)
    // 2. Find the first content display box on this damaged line
    // 3. Find the associated InlineItem with partial text offset if applicable
    auto lineIndex = damagedLineIndex(damagedContent, displayBoxes);
    if (!lineIndex)
        return { };
    auto leadingContentDisplayBoxOnDamagedLine = leadingContentDisplayForLineIndex(*lineIndex, displayBoxes);
    if (!leadingContentDisplayBoxOnDamagedLine) {
        // This is a completely empty line (e.g. <div><span> </span></div> where the whitespace is collapsed).
        return { };
    }
    if (auto inlineItemPositionForLeadingDisplayBox = inlineItemPositionForDisplayBox(*leadingContentDisplayBoxOnDamagedLine, inlineItems)) {
        if (*lineIndex && !*inlineItemPositionForLeadingDisplayBox) {
            // This is clearly not correct (starting position is 0 while the damaged line is not the first one).
            ASSERT_NOT_REACHED();
            lineIndex = 0;
        }
        if (!damagedContent)
            return DamagedLine { *lineIndex, *inlineItemPositionForLeadingDisplayBox };
        // In some rare cases, the leading display box's inline item position is after the actual damage (e.g. when collapsed leading content is removed).
        if (auto adjustedPosition = inlineItemPositionForDamagedContentPosition(*damagedContent, *inlineItemPositionForLeadingDisplayBox, inlineItems))
            return DamagedLine { *lineIndex, *adjustedPosition };
    }
    return { };
}

static std::optional<DamagedLine> leadingInlineItemPositionOnLastLine(const InlineItems& inlineItems, const InlineDisplay::Boxes& displayBoxes)
{
    return leadingInlineItemPositionForDamage({ }, inlineItems, displayBoxes);
}

static std::optional<DamagedLine> leadingInlineItemPositionByDamagedBox(DamagedContent damagedContent, const InlineItems& inlineItems, const InlineDisplay::Boxes& displayBoxes)
{
    return leadingInlineItemPositionForDamage(damagedContent, inlineItems, displayBoxes);
}

static InlineDamage::TrailingDisplayBoxList trailingDisplayBoxesForDamagedLines(size_t damagedLineIndex, const InlineDisplay::Content& displayContent)
{
    auto trailingDisplayBoxes = InlineDamage::TrailingDisplayBoxList { };
    auto& lines = displayContent.lines;
    auto& boxes = displayContent.boxes;
    for (size_t lineIndex = damagedLineIndex; lineIndex < lines.size(); ++lineIndex) {
        auto lastDisplayBoxIndexForLine = lines[lineIndex].firstBoxIndex() + lines[lineIndex].boxCount() - 1;
        if (lastDisplayBoxIndexForLine >= boxes.size()) {
            ASSERT_NOT_REACHED();
            return { };
        }
        trailingDisplayBoxes.append(boxes[lastDisplayBoxIndexForLine]);
    }
    return trailingDisplayBoxes;
}

void InlineInvalidation::updateInlineDamage(InlineDamage::Type type, std::optional<DamagedLine> damagedLine, ShouldApplyRangeLayout shouldApplyRangeLayout)
{
    if (type == InlineDamage::Type::Invalid || !damagedLine)
        return m_inlineDamage.reset();

    m_inlineDamage.setDamageType(type);
    m_inlineDamage.setDamagedPosition({ damagedLine->index, damagedLine->leadingInlineItemPosition });
    if (shouldApplyRangeLayout == ShouldApplyRangeLayout::Yes) 
        m_inlineDamage.setTrailingDisplayBoxes(trailingDisplayBoxesForDamagedLines(damagedLine->index, m_displayContent));
}

static bool isSupportedContent(const Box& layoutBox)
{
    return is<InlineTextBox>(layoutBox) || layoutBox.isLineBreakBox();
}

bool InlineInvalidation::applyFullDamageIfNeeded(const Box& layoutBox)
{
    if (!isSupportedContent(layoutBox)) {
        ASSERT_NOT_REACHED();
        updateInlineDamage(InlineDamage::Type::Invalid, { });
        return true;
    }

    if (displayBoxes().isEmpty()) {
        ASSERT_NOT_REACHED();
        updateInlineDamage(InlineDamage::Type::Invalid, { });
        return true;
    }

    if (m_inlineItems.isEmpty()) {
        // We must be under memory pressure.
        updateInlineDamage(InlineDamage::Type::Invalid, { });
        return true;
    }

    return false;
}

bool InlineInvalidation::textInserted(const InlineTextBox& newOrDamagedInlineTextBox, std::optional<size_t> offset)
{
    if (applyFullDamageIfNeeded(newOrDamagedInlineTextBox))
        return false;

    auto damagedLine = std::optional<DamagedLine> { };
    if (offset) {
        // Existing text box got modified. Dirty all the way up to the damaged position's line.
        damagedLine = leadingInlineItemPositionByDamagedBox({ newOrDamagedInlineTextBox, *offset }, m_inlineItems, displayBoxes());
    } else if (!newOrDamagedInlineTextBox.nextInFlowSibling()) {
        // New text box got appended. Let's dirty the last line.
        damagedLine = leadingInlineItemPositionOnLastLine(m_inlineItems, displayBoxes());
    } else {
        damagedLine = DamagedLine { };
        // New text box got inserted. Let's damage existing content starting from the previous sibling.
        if (auto* previousSibling = newOrDamagedInlineTextBox.previousInFlowSibling())
            damagedLine = leadingInlineItemPositionByDamagedBox({ *previousSibling }, m_inlineItems, displayBoxes());
    }

    updateInlineDamage(!damagedLine ? InlineDamage::Type::Invalid : InlineDamage::Type::NeedsContentUpdateAndLineLayout, damagedLine, offset ? ShouldApplyRangeLayout::No : ShouldApplyRangeLayout::Yes);
    return damagedLine.has_value();
}

bool InlineInvalidation::textWillBeRemoved(const InlineTextBox& damagedInlineTextBox, std::optional<size_t> offset)
{
    if (applyFullDamageIfNeeded(damagedInlineTextBox))
        return false;

    auto damagedLine = leadingInlineItemPositionByDamagedBox({ damagedInlineTextBox, offset.value_or(0), DamagedContent::Type::Removal }, m_inlineItems, displayBoxes());
    updateInlineDamage(!damagedLine ? InlineDamage::Type::Invalid : InlineDamage::Type::NeedsContentUpdateAndLineLayout, damagedLine, ShouldApplyRangeLayout::Yes);
    return damagedLine.has_value();
}

bool InlineInvalidation::inlineLevelBoxInserted(const Box& layoutBox)
{
    if (applyFullDamageIfNeeded(layoutBox))
        return false;

    auto damagedLine = std::optional<DamagedLine> { };
    if (!layoutBox.nextInFlowSibling()) {
        // New box got appended. Let's dirty the last line.
        damagedLine = leadingInlineItemPositionOnLastLine(m_inlineItems, displayBoxes());
    } else {
        damagedLine = DamagedLine { };
        // New box got inserted. Let's damage existing content starting from the previous sibling.
        if (auto* previousSibling = layoutBox.previousInFlowSibling())
            damagedLine = leadingInlineItemPositionByDamagedBox({ *previousSibling }, m_inlineItems, displayBoxes());
    }
    updateInlineDamage(!damagedLine ? InlineDamage::Type::Invalid : InlineDamage::Type::NeedsContentUpdateAndLineLayout, damagedLine, ShouldApplyRangeLayout::Yes);
    return damagedLine.has_value();
}

bool InlineInvalidation::inlineLevelBoxWillBeRemoved(const Box& layoutBox)
{
    if (applyFullDamageIfNeeded(layoutBox))
        return false;

    auto damagedLine = leadingInlineItemPositionByDamagedBox({ layoutBox, { }, DamagedContent::Type::Removal }, m_inlineItems, displayBoxes());
    updateInlineDamage(!damagedLine ? InlineDamage::Type::Invalid : InlineDamage::Type::NeedsContentUpdateAndLineLayout, damagedLine, ShouldApplyRangeLayout::Yes);
    return damagedLine.has_value();
}

void InlineInvalidation::horizontalConstraintChanged()
{
    m_inlineDamage.setDamageType(InlineDamage::Type::NeedsLineLayout);
}

}
}
