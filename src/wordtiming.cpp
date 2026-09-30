#include "wordtiming.h"

#include <algorithm>
#include <cmath>

namespace {
struct Pause {
    int start; // frames
    int end;
    double center() const { return (start + end) * 0.5; }
};

struct Anchor {
    size_t token; // boundary after this token
    int pauseStart;
    int pauseEnd;
};
}

std::vector<WordSpanTime> estimateWordTimes(const float *audio, size_t count, int sampleRate,
                                            const std::vector<double> &weights,
                                            const std::vector<double> &pauseAfter)
{
    const size_t n = weights.size();
    std::vector<WordSpanTime> out(n);
    if (n == 0 || count == 0 || sampleRate <= 0)
        return out;

    // 10 ms analysis frames
    const int frame = std::max(1, sampleRate / 100);
    const int frames = int(count / size_t(frame));
    if (frames < 2) {
        return out;
    }
    std::vector<float> rms(size_t(frames), 0.f);
    float peak = 0.f;
    for (int f = 0; f < frames; ++f) {
        double acc = 0;
        const float *p = audio + size_t(f) * size_t(frame);
        for (int i = 0; i < frame; ++i)
            acc += double(p[i]) * p[i];
        rms[size_t(f)] = float(std::sqrt(acc / frame));
        peak = std::max(peak, rms[size_t(f)]);
    }
    const float thr = std::max(peak * 0.045f, 0.0015f);

    int vs = 0;
    while (vs < frames && rms[size_t(vs)] <= thr)
        ++vs;
    int ve = frames;
    while (ve > vs && rms[size_t(ve - 1)] <= thr)
        --ve;
    if (ve <= vs) {
        vs = 0;
        ve = frames;
    }

    // Internal pauses (>= 60 ms of low energy)
    std::vector<Pause> pauses;
    {
        int runStart = -1;
        for (int f = vs; f < ve; ++f) {
            const bool quiet = rms[size_t(f)] <= thr;
            if (quiet && runStart < 0)
                runStart = f;
            if (!quiet && runStart >= 0) {
                if (f - runStart >= 6)
                    pauses.push_back({runStart, f});
                runStart = -1;
            }
        }
    }

    auto wsum = [&](size_t a, size_t b) { // units for tokens [a, b] incl. inner pauses
        double u = 0;
        for (size_t j = a; j <= b; ++j) {
            u += weights[j];
            if (j < b)
                u += pauseAfter[j];
        }
        return u;
    };

    const double totalUnits = wsum(0, n - 1);
    std::vector<Anchor> anchors;
    if (totalUnits > 0 && !pauses.empty()) {
        const double framesPerUnit = double(ve - vs) / totalUnits;
        const double tol = std::max(40.0, 0.2 * (ve - vs));
        // Expected pause centres for each punctuation boundary
        std::vector<std::pair<size_t, double>> bounds;
        double cum = 0;
        for (size_t i = 0; i + 1 < n; ++i) {
            cum += weights[i];
            if (pauseAfter[i] > 0)
                bounds.push_back({i, vs + (cum + pauseAfter[i] * 0.5) * framesPerUnit});
            cum += pauseAfter[i];
        }
        size_t nextPause = 0;
        for (size_t b = 0; b < bounds.size(); ++b) {
            const double expect = bounds[b].second;
            size_t best = pauses.size();
            double bestDist = tol;
            for (size_t k = nextPause; k < pauses.size(); ++k) {
                const double d = std::abs(pauses[k].center() - expect);
                if (d < bestDist) {
                    bestDist = d;
                    best = k;
                }
            }
            if (best == pauses.size())
                continue;
            // Leave the pause to the following boundary if it fits that one better.
            if (b + 1 < bounds.size() && std::abs(pauses[best].center() - bounds[b + 1].second) < bestDist)
                continue;
            anchors.push_back({bounds[b].first, pauses[best].start, pauses[best].end});
            nextPause = best + 1;
        }
    }

    // Distribute tokens between anchors.
    auto place = [&](size_t a, size_t b, int startFrame, int endFrame) {
        const double units = wsum(a, b);
        const double span = std::max(0, endFrame - startFrame);
        double t = startFrame;
        for (size_t j = a; j <= b; ++j) {
            const double scale = units > 0 ? span / units : (j == a ? span : 0);
            const double w = units > 0 ? weights[j] : (j == a ? 1.0 : 0.0);
            out[j].start = int(std::lround(t * frame));
            t += w * scale;
            out[j].end = int(std::lround(t * frame));
            if (j < b && units > 0)
                t += pauseAfter[j] * scale;
        }
    };

    size_t first = 0;
    int segStart = vs;
    for (const Anchor &an : anchors) {
        if (an.token < first)
            continue;
        place(first, an.token, segStart, an.pauseStart);
        first = an.token + 1;
        segStart = an.pauseEnd;
    }
    if (first < n)
        place(first, n - 1, segStart, ve);
    return out;
}
