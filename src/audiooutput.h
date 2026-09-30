#pragma once

#include "segment.h"

#include <QString>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

struct pa_simple;

// Low-latency PCM output through PulseAudio / PipeWire-Pulse.
//
// The key job of this class is answering "what is the listener hearing right
// now?" precisely: every chunk written to the server is logged with the
// segment/offset it came from, and the audible position is
// (frames written - server-reported latency). Highlighting is driven from
// that, so it is locked to real playback and cannot drift.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    struct Position {
        SegmentPtr segment;
        int offset = 0;     // frame offset inside segment
    };

    void setActive(bool active);        // playback session running (write silence while starved)
    void enqueue(SegmentPtr segment);
    void clear();                       // stop immediately and drop everything
    void setPaused(bool paused);        // pause/resume at the exact audible sample
    Position position();                // segment == nullptr if nothing audible yet
    bool finished();                    // everything enqueued has been heard
    int queuedSegments();               // segments not yet fully heard
    void setLatencyOffsetMs(int ms);
    QString lastError();

private:
    struct Record {
        quint64 start;      // stream frame index
        SegmentPtr segment; // null for silence
        int offset;
        int frames;
    };

    void run();
    bool openStream(int rate);
    void closeStream();
    quint64 currentLatencyUs();
    Position locateLocked(quint64 latencyUs, bool resumePoint);
    void trimLocked(const SegmentPtr &audible);

    std::thread m_thread;
    std::mutex m_mutex;
    std::mutex m_paMutex;               // guards m_pa lifetime vs. latency queries
    std::condition_variable m_cv;

    pa_simple *m_pa = nullptr;
    int m_rate = 0;
    quint64 m_written = 0;              // frames written since the stream was opened
    std::deque<Record> m_records;
    std::deque<SegmentPtr> m_queue;     // from the oldest possibly-audible segment onwards
    size_t m_writeIdx = 0;              // write cursor: segment index in m_queue ...
    int m_writeOffset = 0;              // ... and frame offset inside it
    bool m_active = false;
    bool m_paused = false;
    bool m_flush = false;
    bool m_quit = false;
    int m_latencyOffsetMs = 0;
    QString m_error;
};
