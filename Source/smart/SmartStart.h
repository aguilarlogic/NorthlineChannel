#pragma once

#include "SmartCapture.h"
#include <memory>

namespace nl::smart
{
// Message-thread controller for Smart Start: Listen starts a capture, the finished capture is
// analysed on a background thread, and the result is handed back once to the processor, which
// applies it as one undo step. Nothing here runs on the audio thread except the capture buffer.
class SmartStart
{
public:
    enum class Phase : int { Idle = 0, Capturing, Analysing, Done };

    SmartStart() = default;
    ~SmartStart();

    // Message thread, while audio is stopped.
    void prepare (double sampleRate);

    // Message thread. Starts a capture of the given source. Returns false while one is running.
    bool listen (Source source);

    // Message thread. Abandons a capture or an analysis that has not been applied yet.
    void cancel();

    // Message thread, polled by the processor's timer. `current` holds the plain value of every
    // parameter at this moment. Returns true once a new result is ready (read it with lastResult()).
    bool service (const std::array<float, P::count>& current);

    Phase getPhase() const noexcept { return phase; }
    Source getSource() const noexcept { return source; }
    const Result& lastResult() const noexcept { return result; }

    // The one-line status the panel shows.
    juce::String statusText() const;

    SmartCapture& getCapture() noexcept { return capture; }

private:
    class Worker;

    SmartCapture capture;
    std::unique_ptr<Worker> worker;
    Phase phase = Phase::Idle;
    Source source = Source::Vocal;
    Result result;
    double fs = 48000.0;
};
} // namespace nl::smart
