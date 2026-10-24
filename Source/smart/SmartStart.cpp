#include "SmartStart.h"

namespace nl::smart
{
// Runs one analysis off the message thread. It owns its copy of the samples, so nothing it touches
// is shared with the audio thread or with the capture buffer.
class SmartStart::Worker : public juce::Thread
{
public:
    Worker (std::vector<float> samplesIn, double sampleRate, Source s, const std::array<float, P::count>& values)
        : juce::Thread ("Northline Smart Start"), samples (std::move (samplesIn)), fs (sampleRate), source (s), current (values)
    {
    }

    void run() override { output = analyze (samples.data(), (int) samples.size(), fs, source, current); }

    Result output;

private:
    std::vector<float> samples;
    double fs;
    Source source;
    std::array<float, P::count> current;
};

SmartStart::SmartStart() = default;

SmartStart::~SmartStart()
{
    if (worker != nullptr) worker->waitForThreadToExit (-1);
}

void SmartStart::prepare (double sampleRate)
{
    if (worker != nullptr) worker->waitForThreadToExit (-1);
    worker.reset();
    fs = sampleRate;
    capture.prepare (sampleRate);
    phase = Phase::Idle;
}

bool SmartStart::listen (Source s)
{
    if (phase == Phase::Capturing || phase == Phase::Analysing) return false;
    source = s;
    capture.request();
    phase = Phase::Capturing;
    return true;
}

void SmartStart::cancel()
{
    capture.cancel();
    if (worker != nullptr) worker->waitForThreadToExit (-1);
    worker.reset();
    phase = Phase::Idle;
}

bool SmartStart::service (const std::array<float, P::count>& current)
{
    if (phase == Phase::Capturing && capture.ready())
    {
        worker = std::make_unique<Worker> (capture.take(), fs, source, current);
        worker->startThread();
        phase = Phase::Analysing;
    }

    if (phase == Phase::Analysing && worker != nullptr && ! worker->isThreadRunning())
    {
        worker->waitForThreadToExit (1000);
        result = worker->output;
        worker.reset();
        phase = Phase::Done;
        return result.valid;
    }
    return false;
}

juce::String SmartStart::statusText() const
{
    switch (phase)
    {
        case Phase::Idle:      return "Pick a source and press Listen.";
        case Phase::Capturing: return capture.getState() == SmartCapture::Recording
                                   ? juce::String ("Listening to ") + juce::String (sourceName (source)) + juce::String::formatted (" for %.0f s...", kCaptureSeconds)
                                   : "Waiting for audio. Start playback.";
        case Phase::Analysing: return "Analysing the capture...";
        case Phase::Done:      return result.summary;
    }
    return {};
}
} // namespace nl::smart
