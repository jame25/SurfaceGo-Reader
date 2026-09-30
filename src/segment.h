#pragma once

#include "wordtiming.h"

#include <QMetaType>
#include <cstdint>
#include <memory>
#include <vector>

// One synthesized sentence, ready for playback.
struct AudioSegment {
    int sentence = -1;
    quint64 generation = 0;
    int sampleRate = 22050;
    std::vector<int16_t> samples;    // speech followed by the inter-sentence pause
    std::vector<WordSpanTime> words; // one entry per word token of the sentence

    int frames() const { return int(samples.size()); }

    // Index of the word being spoken at `offset` (samples). Between words the
    // previous word stays highlighted; -1 before the first word starts.
    int wordAt(int offset) const
    {
        int found = -1;
        for (int i = 0; i < int(words.size()); ++i) {
            if (words[size_t(i)].end <= words[size_t(i)].start)
                continue; // silent token (punctuation only)
            if (words[size_t(i)].start <= offset)
                found = i;
            else
                break;
        }
        return found;
    }
};

using SegmentPtr = std::shared_ptr<const AudioSegment>;
Q_DECLARE_METATYPE(SegmentPtr)
