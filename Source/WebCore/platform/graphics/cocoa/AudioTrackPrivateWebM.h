/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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

#if ENABLE(MEDIA_SOURCE)

#include "AudioTrackPrivate.h"
#include <webm/dom_types.h>

namespace WebCore {

struct AudioInfo;

class AudioTrackPrivateWebM final : public AudioTrackPrivate {
public:
    static Ref<AudioTrackPrivateWebM> create(webm::TrackEntry&&);
    virtual ~AudioTrackPrivateWebM() = default;

    AtomString id() const final;
    AtomString label() const final;
    AtomString language() const final;
    int trackIndex() const final;
    std::optional<uint64_t> trackUID() const final;
    std::optional<bool> defaultEnabled() const final;
    std::optional<MediaTime> codecDelay() const;
    void setDiscardPadding(const MediaTime&);
    std::optional<MediaTime> discardPadding() const;

private:
    AudioTrackPrivateWebM(webm::TrackEntry&&);

    String codec() const;
    uint32_t sampleRate() const;
    uint32_t numberOfChannels() const;
    void setFormatDescription(Ref<AudioInfo>&&) final;
    void updateConfiguration();

    webm::TrackEntry m_track;
    RefPtr<AudioInfo> m_formatDescription;
    MediaTime m_discardPadding { MediaTime::invalidTime() };
    mutable AtomString m_trackID;
    mutable AtomString m_label;
    mutable AtomString m_language;
};

}

#endif // ENABLE(MEDIA_SOURCE)
