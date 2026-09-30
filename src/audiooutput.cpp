#include "audiooutput.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <algorithm>
#include <chrono>
#include <cstring>

using namespace std::chrono_literals;

AudioOutput::AudioOutput()
{
    m_thread = std::thread([this] { run(); });
}

AudioOutput::~AudioOutput()
{
    {
        std::lock_guard lk(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    m_thread.join();
    closeStream();
}

bool AudioOutput::openStream(int rate)
{
    closeStream();
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = uint32_t(rate);
    ss.channels = 1;
    pa_buffer_attr attr;
    attr.maxlength = uint32_t(-1);
    attr.tlength = uint32_t(rate * 2 * 100 / 1000);  // ~100 ms server-side buffer
    attr.prebuf = uint32_t(rate * 2 * 30 / 1000);    // start playing after 30 ms
    attr.minreq = uint32_t(-1);
    attr.fragsize = uint32_t(-1);
    int err = 0;
    pa_simple *pa = pa_simple_new(nullptr, "SurfaceGo Reader", PA_STREAM_PLAYBACK, nullptr, "Read aloud",
                                  &ss, nullptr, &attr, &err);
    std::lock_guard g(m_paMutex);
    m_pa = pa;
    if (!pa) {
        std::lock_guard lk(m_mutex);
        m_error = QStringLiteral("Audio output failed: %1").arg(QString::fromUtf8(pa_strerror(err)));
        return false;
    }
    return true;
}

void AudioOutput::closeStream()
{
    std::lock_guard g(m_paMutex);
    if (m_pa) {
        pa_simple_free(m_pa);
        m_pa = nullptr;
    }
}

quint64 AudioOutput::currentLatencyUs()
{
    std::lock_guard g(m_paMutex);
    if (!m_pa)
        return 0;
    int err = 0;
    const pa_usec_t l = pa_simple_get_latency(m_pa, &err);
    return l == pa_usec_t(-1) ? 0 : quint64(l);
}

AudioOutput::Position AudioOutput::locateLocked(quint64 latencyUs, bool resumePoint)
{
    if (m_records.empty() || m_rate <= 0)
        return {};
    qint64 audible = qint64(m_written) - qint64(latencyUs * quint64(m_rate) / 1000000);
    if (!resumePoint)
        audible -= qint64(m_latencyOffsetMs) * m_rate / 1000;

    auto firstSegmentFrom = [&](size_t i) -> Position {
        for (; i < m_records.size(); ++i)
            if (m_records[i].segment)
                return {m_records[i].segment, m_records[i].offset};
        return {};
    };

    // Last record starting at or before the audible frame.
    size_t idx = m_records.size();
    for (size_t i = m_records.size(); i-- > 0;) {
        if (qint64(m_records[i].start) <= audible) {
            idx = i;
            break;
        }
    }
    if (idx == m_records.size()) // nothing audible yet
        return resumePoint ? firstSegmentFrom(0) : Position{};

    const Record &r = m_records[idx];
    const qint64 delta = audible - qint64(r.start);
    if (!r.segment)
        return resumePoint ? firstSegmentFrom(idx + 1) : Position{};
    if (delta >= r.frames) {
        // Everything written so far has been heard.
        if (resumePoint)
            return firstSegmentFrom(idx + 1);
        return {r.segment, std::min(r.segment->frames(), r.offset + r.frames)};
    }
    return {r.segment, r.offset + int(delta)};
}

void AudioOutput::trimLocked(const SegmentPtr &audible)
{
    if (!audible)
        return;
    size_t j = 0;
    while (j < m_queue.size() && j < m_writeIdx && m_queue[j] != audible)
        ++j;
    if (j < m_queue.size() && m_queue[j] == audible && j > 0) {
        m_queue.erase(m_queue.begin(), m_queue.begin() + qsizetype(j));
        m_writeIdx -= j;
    }
    // Keep ~2 s of history in the record log.
    const quint64 keepFrom = m_written > quint64(m_rate) * 2 + 1 ? m_written - quint64(m_rate) * 2 : 0;
    while (m_records.size() > 4 && m_records.front().start + quint64(m_records.front().frames) < keepFrom)
        m_records.pop_front();
}

AudioOutput::Position AudioOutput::position()
{
    const quint64 lat = currentLatencyUs();
    std::lock_guard lk(m_mutex);
    if (m_paused)
        return {};
    Position p = locateLocked(lat, false);
    trimLocked(p.segment);
    return p;
}

bool AudioOutput::finished()
{
    const quint64 lat = currentLatencyUs();
    std::lock_guard lk(m_mutex);
    if (m_writeIdx < m_queue.size())
        return false;
    for (size_t i = m_records.size(); i-- > 0;) {
        const Record &r = m_records[i];
        if (!r.segment)
            continue;
        const qint64 audible = qint64(m_written) - qint64(lat * quint64(std::max(1, m_rate)) / 1000000);
        return audible >= qint64(r.start) + r.frames;
    }
    return true;
}

int AudioOutput::queuedSegments()
{
    std::lock_guard lk(m_mutex);
    return int(m_queue.size());
}

void AudioOutput::setActive(bool active)
{
    {
        std::lock_guard lk(m_mutex);
        m_active = active;
    }
    m_cv.notify_all();
}

void AudioOutput::enqueue(SegmentPtr segment)
{
    {
        std::lock_guard lk(m_mutex);
        m_queue.push_back(std::move(segment));
    }
    m_cv.notify_all();
}

void AudioOutput::clear()
{
    {
        std::lock_guard lk(m_mutex);
        m_queue.clear();
        m_writeIdx = 0;
        m_writeOffset = 0;
        m_records.clear();
        m_flush = true;
        m_active = false;
        m_paused = false;
    }
    m_cv.notify_all();
}

void AudioOutput::setPaused(bool paused)
{
    const quint64 lat = paused ? currentLatencyUs() : 0;
    {
        std::lock_guard lk(m_mutex);
        if (m_paused == paused)
            return;
        m_paused = paused;
        if (paused) {
            // Rewind the write cursor to exactly what is audible now, then
            // drop the server buffer so audio stops immediately.
            const Position p = locateLocked(lat, true);
            if (p.segment) {
                const auto it = std::find(m_queue.begin(), m_queue.end(), p.segment);
                if (it != m_queue.end()) {
                    m_queue.erase(m_queue.begin(), it);
                    m_writeIdx = 0;
                    m_writeOffset = p.offset;
                }
            }
            m_records.clear();
            m_flush = true;
        }
    }
    m_cv.notify_all();
}

void AudioOutput::setLatencyOffsetMs(int ms)
{
    std::lock_guard lk(m_mutex);
    m_latencyOffsetMs = ms;
}

QString AudioOutput::lastError()
{
    std::lock_guard lk(m_mutex);
    return m_error;
}

void AudioOutput::run()
{
    std::vector<int16_t> buf;
    auto lastWrite = std::chrono::steady_clock::now();
    std::unique_lock lk(m_mutex);
    while (!m_quit) {
        if (m_flush) {
            m_flush = false;
            lk.unlock();
            {
                std::lock_guard g(m_paMutex);
                int err = 0;
                if (m_pa)
                    pa_simple_flush(m_pa, &err);
            }
            lk.lock();
            continue;
        }

        const bool haveData = m_writeIdx < m_queue.size();
        const bool wantWrite = !m_paused && (haveData || (m_active && m_pa));
        if (!wantWrite) {
            // Release the audio device after a while so PipeWire can suspend it.
            if (m_pa && !m_active && !m_paused && std::chrono::steady_clock::now() - lastWrite > 8s) {
                lk.unlock();
                closeStream();
                lk.lock();
                m_rate = 0;
                m_records.clear();
                continue;
            }
            m_cv.wait_for(lk, 1s);
            continue;
        }

        SegmentPtr seg;
        int offset = 0;
        int frames = 0;
        if (haveData) {
            seg = m_queue[m_writeIdx];
            if (!m_pa || seg->sampleRate != m_rate) {
                const int rate = seg->sampleRate;
                lk.unlock();
                const bool ok = openStream(rate);
                lk.lock();
                m_rate = ok ? rate : 0;
                m_written = 0;
                m_records.clear();
                if (!ok)
                    m_cv.wait_for(lk, 1s);
                continue;
            }
            const int chunk = std::max(64, m_rate * 15 / 1000);
            offset = m_writeOffset;
            frames = std::min(chunk, seg->frames() - offset);
            if (frames <= 0) {
                ++m_writeIdx;
                m_writeOffset = 0;
                continue;
            }
            buf.assign(seg->samples.begin() + offset, seg->samples.begin() + offset + frames);
            m_writeOffset += frames;
            if (m_writeOffset >= seg->frames()) {
                ++m_writeIdx;
                m_writeOffset = 0;
            }
        } else {
            // Synthesis is behind: keep the stream running with silence so the
            // clock stays continuous.
            frames = std::max(64, m_rate * 15 / 1000);
            buf.assign(size_t(frames), 0);
        }

        m_records.push_back({m_written, seg, offset, frames});
        pa_simple *pa = m_pa; // only this thread replaces m_pa
        lk.unlock();
        int err = 0;
        const int r = pa_simple_write(pa, buf.data(), size_t(frames) * sizeof(int16_t), &err);
        lk.lock();
        m_written += quint64(frames);
        lastWrite = std::chrono::steady_clock::now();
        if (r < 0) {
            m_error = QStringLiteral("Audio write failed: %1").arg(QString::fromUtf8(pa_strerror(err)));
            lk.unlock();
            closeStream();
            lk.lock();
            m_rate = 0;
            m_records.clear();
        }
    }
}
