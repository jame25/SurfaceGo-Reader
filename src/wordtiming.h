#pragma once

#include <cstddef>
#include <vector>

// Estimates when each word starts/ends inside one synthesized sentence.
//
// Piper models output audio only (no alignments), so word boundaries are
// derived from the audio itself: speech onset/offset and the pauses that the
// model inserts at clause punctuation are detected from the signal energy and
// used as hard anchors; words between anchors are placed proportionally to
// their phoneme-based weights. Sentence boundaries are exact because every
// sentence is synthesized as a separate audio segment.
struct WordSpanTime {
    int start = 0; // sample offsets inside the segment
    int end = 0;
};

std::vector<WordSpanTime> estimateWordTimes(const float *audio, size_t count, int sampleRate,
                                            const std::vector<double> &weights,
                                            const std::vector<double> &pauseAfter);
