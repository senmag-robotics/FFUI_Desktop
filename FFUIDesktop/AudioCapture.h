#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <chrono>

//Beep()/waveIn* both live in winmm.lib.
#pragma comment(lib, "winmm.lib")

//A thin wrapper around the legacy winmm waveIn* API for short, button-gated recordings.
//WASAPI (IAudioClient/IAudioCaptureClient) would give lower latency and more control, but
//needs a lot more COM setup for what is here a simple "record while a button is held, then
//hand the whole buffer to speech recognition" use case - waveIn's blocking-buffer model fits
//that directly and needs no COM at all.
class AudioCapture {
public:
	AudioCapture();
	~AudioCapture();

	//Opens the default microphone and starts filling buffers. Call from the voice worker
	//thread only, never the haptic thread - this touches Win32 device handles and can block
	//briefly while the driver opens the device.
	bool start();

	//Stops recording and returns everything captured since start(). Blocks briefly while the
	//driver flushes its last buffer. Returns an empty vector if start() was never called or
	//already failed.
	std::vector<int16_t> stop();

	bool isRecording() const { return recording.load(); }

	//Mono 16-bit PCM at this rate is what WhisperTranscriber/whisper.cpp expects (also what
	//SAPI's SpeechRecognizer, no longer used, expected too - see WhisperTranscriber.h).
	static constexpr int SAMPLE_RATE_HZ = 16000;

	//Hard safety cap on a single recording. Not enforced inside AudioCapture itself - the
	//caller (VoiceAssistant's worker loop) owns the timing decision and calls stop() once
	//this much wall-clock time has passed, so a stuck AUX button can't record forever.
	static constexpr std::chrono::seconds MAX_RECORD_DURATION{ 20 };

	//Short audible cues. Beep() is synchronous (blocks the calling thread for its duration) -
	//always call these from the voice worker thread, never the haptic thread.
	static void playListeningCue();   //short high beep - "I'm listening"
	static void playProcessingCue();  //short low beep - "got it, thinking"
	static void playNotReadyCue();    //two short low beeps - "the assistant isn't set up yet", distinct from the other two

	//Dictation's own listening/processing cues - "use similar, but distinct sound cues to the
	//AI speech capture", per the request. Same family (short Beep() wrappers marking the same
	//two moments - "I'm listening"/"got it, transcribing") as playListeningCue()/
	//playProcessingCue() above, but shaped differently enough to be told apart by ear alone:
	//a quick double-blip for listening (vs. the AI's single beep) and a single, differently-
	//pitched beep for processing (vs. the AI's 600Hz). Exact pitches/durations unverified - tune
	//by feel, same as every other haptic/audio constant in this codebase.
	static void playDictateListeningCue();
	static void playDictateProcessingCue();

	//A single soft, brief tick - played on a repeat while waiting on Claude's reply (see
	//VoiceAssistant::runExchange()'s dedicated ticker thread), so a multi-second round trip
	//doesn't feel like nothing is happening. Deliberately shorter and softer than the other
	//three cues above, which each mark a one-off event - this one repeats every couple of
	//seconds, so it needs to read as ambient reassurance rather than a fresh alert each time.
	//Also safe to call concurrently with any of the other cues above (from a different thread) -
	//Beep() itself is just a thin wrapper around the console/motherboard beep device and holds
	//no state of its own, unlike the instance methods on this class.
	static void playThinkingTickCue();

private:
	static constexpr int NUM_BUFFERS = 4;
	static constexpr int BUFFER_MS = 200;          //~200ms of audio per buffer
	static constexpr int BUFFER_SAMPLES = SAMPLE_RATE_HZ * BUFFER_MS / 1000;

	HWAVEIN waveInHandle = nullptr;
	WAVEHDR headers[NUM_BUFFERS]{};
	std::vector<int16_t> headerStorage[NUM_BUFFERS];

	std::vector<int16_t> capturedSamples;
	std::mutex captureMutex;          //guards capturedSamples - both the driver callback and stop() touch it
	std::atomic<bool> recording{ false };

	//waveIn callback - runs on a driver-owned thread. Must stay tiny and non-blocking; the only
	//waveIn* call that's documented as safe to make from inside this callback is waveInAddBuffer.
	static void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
	void onBufferFull(WAVEHDR* header);
};
