# SurfaceGo Reader

A touch-first EPUB/PDF reader that reads books aloud with **Piper** voices and
highlights the spoken **sentence** (green by default) and **word** (bold or
inverted) in sync with the audio. It's built for a Surface Go 3 (i3, Arch Linux, KDE Plasma).

## Why C++ / Qt Quick

The Surface Go 3's dual-core i3 is the bottleneck, so the app is native C++20:

* **Qt 6 Quick (QML)** renders on the GPU and handles touch and flick input well.
  It's already installed on KDE Plasma, so it costs almost no extra disk space.
* **Piper runs in-process** through ONNX Runtime and eSpeak NG (the same pipeline
  as the `piper` binary). There's no Python and no subprocess, the model loads once,
  and synthesis stays several sentences ahead of playback.
* **Poppler** extracts PDF text, and a small built-in ZIP/XHTML parser reads EPUB.
* Audio goes straight to **PipeWire/PulseAudio** (`libpulse-simple`) from its own
  thread.

On a desktop CPU a sentence synthesizes at about 0.03–0.05 real-time factor. The
Surface's i3 is several times slower, which still leaves plenty of headroom.

## Features

| Feature | Where |
|---|---|
| Piper voice models (`.onnx` + `.onnx.json`), multi-speaker aware | Voice panel (footer) |
| Voice selector and speed (`length_scale`) slider/presets | Footer → voice/speed button |
| Sentence highlight in green, yellow, blue, pink or orange | Settings → Appearance |
| Word highlight: bold or inverted colours (e.g. yellow text on a dark box inside a yellow sentence) | Settings → Appearance |
| Highlights driven by the audio clock (no drift) | See *How sync works* |
| Tap any word to read from that sentence | Reader |
| Auto-scroll follows playback (toggle); pauses while you scroll by hand | Header ↓ button |
| Play/Pause, Stop, Previous/Next sentence | Footer |
| Library folder (recursive), change folder in Settings | Library |
| Tap to open, long-press to remove (confirm → moved to Trash) | Library |
| A− / A+ font size, pinch-to-zoom, word-wrap toggle | Header |
| Remembers reading position and scroll position per book | automatic |
| Table of contents (EPUB nav/NCX, PDF outline or pages) | Header ☰ or swipe from left |
| Light, sepia, dark and black backgrounds, with a live preview | Settings → Appearance |
| Remembers window size and maximized/full-screen state between sessions | automatic |
| Highlight-latency offset for Bluetooth headsets | Settings |

## Install on the Surface

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-svg \
                        poppler-qt6 espeak-ng onnxruntime-cpu libpulse
cd surfacego_reader/packaging
makepkg -si            # builds and installs /usr/bin/surfacego-reader + launcher
```

Or build without packaging:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/surfacego-reader
```

## Voices

Put Piper voices (each needs both files) into:

```
~/.local/share/surfacego-reader/voices/
    voice.onnx
    voice.onnx.json
```

You can pick a different folder in Settings. Voices are listed in the footer's
voice panel, and the first one found is used by default.

## Books

Put `.epub` / `.pdf` files in `~/Books` (or choose another library folder in
Settings, which also works on SD cards under `/run/media`). Sub-folders are
scanned, and new files show up automatically.

PDFs are converted to reflowable text. The converter joins lines into paragraphs,
de-hyphenates, drops page numbers and running headers/footers, and removes TOC
dot leaders. Scanned PDFs without a text layer need OCR first (e.g.
`ocrmypdf`). A parsed book is cached in `~/.cache/surfacego-reader/books/`, so the
second open is instant.

## How sync works

1. The book is split into sentences, and each sentence is synthesized as its own
   audio segment, so sentence boundaries in the audio are exact.
2. The audio thread logs every chunk it writes to PipeWire (which segment, which
   offset). The audible position is *frames written − server-reported latency*.
   The highlight polls this every 30 ms, so it follows what you actually hear
   and can't drift. Pause flushes the server buffer and resumes from the exact
   audible sample.
3. Piper models don't output word timings, so word boundaries are estimated
   inside each sentence. The pauses the voice makes at punctuation are found in
   the waveform and used as anchors, and words between anchors are placed by
   their phoneme count (from eSpeak).

This was checked end to end by recording the output of a PipeWire null sink:
each sentence highlight appeared within about 15–55 ms of the recorded speech
onset, and that offset didn't grow over the playback.

If you use Bluetooth headphones (which add latency the audio server can't fully
report), adjust **Settings → Highlight timing offset**.

## Files

| What | Where |
|---|---|
| Settings, window size and reading positions | `~/.config/surfacego-reader/surfacego-reader.conf` |
| Parsed-book cache | `~/.cache/surfacego-reader/books/` |
| Voices (default) | `~/.local/share/surfacego-reader/voices/` |

## Tuning

* `SGREADER_TTS_THREADS=N` sets the number of inference threads (default is
  half the logical CPUs, so 2 on the Surface Go 3 i3).
* `F11` toggles full screen and `Esc` goes back.

## Development

```
src/book.*            document model, sentence/word segmentation, cache
src/epubloader.*      EPUB (OPF/spine, nav/NCX TOC, anchors) + zipreader.* + htmltext.*
src/pdfloader.*       Poppler text → lines → paragraphs
src/phonemizer.*      eSpeak NG → Piper-compatible IPA + phoneme ids
src/pipervoice.*      ONNX Runtime inference of Piper VITS models
src/wordtiming.*      word boundary estimation from audio + phoneme weights
src/synthworker.*     synthesis thread (generation-based cancellation)
src/audiooutput.*     PulseAudio/PipeWire output + audible-position clock
src/reader.*          playback state machine, highlight, persistence (QML API)
qml/                  touch UI (Material style)
```

`sgreader-selftest` (built by default, not installed) exercises the pipeline
without the GUI:

```bash
./build/sgreader-selftest split                               # sentence-splitter tests
./build/sgreader-selftest book ~/Books/some.epub              # parse + print sentences
./build/sgreader-selftest speak voices/voice.onnx "Hello, world." out.wav
```

The UI icons are paths from Google's Material Design Icons (Apache License 2.0).
