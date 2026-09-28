#include "AudioCapture.h"
#include "ClaudeCliClient.h"   //just for the wideToUtf8() helper - see the device-name log in start()
#include <iostream>

AudioCapture::AudioCapture() {}

AudioCapture::~AudioCapture() {
	if (recording.load()) {
		stop();
	}
}

bool AudioCapture::start() {
	if (recording.load()) return false;  //already recording - callers are also re-entrancy guarded upstream

	{
		std::lock_guard<std::mutex> lock(captureMutex);
		capturedSamples.clear();
	}

	WAVEFORMATEX wfx{};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 1;
	wfx.nSamplesPerSec = SAMPLE_RATE_HZ;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.cbSize = 0;

	//WAVE_MAPPER is the documented Windows device ID that always resolves to whatever the OS
	//currently considers the default recording device (Settings > Sound > Input) - this already
	//is "the system default microphone", not something that needs to be separately selected.
	MMRESULT openResult = waveInOpen(&waveInHandle, WAVE_MAPPER, &wfx,
		reinterpret_cast<DWORD_PTR>(&AudioCapture::waveInProc),
		reinterpret_cast<DWORD_PTR>(this),
		CALLBACK_FUNCTION);

	if (openResult != MMSYSERR_NOERROR) {
		waveInHandle = nullptr;
		return false;
	}

	//Resolve and print which physical device WAVE_MAPPER actually opened, so that's directly
	//verifiable at runtime (e.g. against Settings > Sound > Input) rather than just trusted from
	//the comment above - useful if recognition accuracy ever seems off and the cause might be
	//"listening to the wrong microphone" rather than the recognizer itself. Best-effort only -
	//if either call fails, capture still proceeds normally, just without this one log line.
	UINT resolvedDeviceId = 0;
	if (waveInGetID(waveInHandle, &resolvedDeviceId) == MMSYSERR_NOERROR) {
		WAVEINCAPSW caps{};
		if (waveInGetDevCapsW(resolvedDeviceId, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			std::cout << "[Voice Assistant] Recording from default microphone: "
				<< ClaudeCliClient::wideToUtf8(caps.szPname) << std::endl;
		}
	}

	for (int i = 0; i < NUM_BUFFERS; i++) {
		headerStorage[i].assign(BUFFER_SAMPLES, 0);

		WAVEHDR& header = headers[i];
		header = WAVEHDR{};
		header.lpData = reinterpret_cast<LPSTR>(headerStorage[i].data());
		header.dwBufferLength = static_cast<DWORD>(headerStorage[i].size() * sizeof(int16_t));

		waveInPrepareHeader(waveInHandle, &header, sizeof(WAVEHDR));
		waveInAddBuffer(waveInHandle, &header, sizeof(WAVEHDR));
	}

	//Set this before waveInStart so the very first callback (which can in principle fire
	//almost immediately) already sees recording==true and re-queues its buffer correctly.
	recording.store(true);

	MMRESULT startResult = waveInStart(waveInHandle);
	if (startResult != MMSYSERR_NOERROR) {
		recording.store(false);
		for (int i = 0; i < NUM_BUFFERS; i++) {
			waveInUnprepareHeader(waveInHandle, &headers[i], sizeof(WAVEHDR));
		}
		waveInClose(waveInHandle);
		waveInHandle = nullptr;
		return false;
	}

	return true;
}

std::vector<int16_t> AudioCapture::stop() {
	//Flip this first: any buffer-full callback that arrives after this point will see
	//recording==false and won't try to hand its buffer back to a device we're about to close.
	if (!recording.exchange(false)) {
		return {};  //wasn't recording (start() failed, or stop() called twice)
	}

	if (waveInHandle) {
		waveInStop(waveInHandle);

		//waveInReset forces every outstanding buffer back to us (each triggers one last
		//WIM_DATA callback) before it returns, so by the time we get here every header is
		//safe to unprepare. This is the one part of the waveIn dance that's worth specifically
		//testing on real hardware - driver behaviour around reset timing does vary.
		waveInReset(waveInHandle);

		for (int i = 0; i < NUM_BUFFERS; i++) {
			waveInUnprepareHeader(waveInHandle, &headers[i], sizeof(WAVEHDR));
		}

		waveInClose(waveInHandle);
		waveInHandle = nullptr;
	}

	std::lock_guard<std::mutex> lock(captureMutex);
	return std::move(capturedSamples);
}

void CALLBACK AudioCapture::waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	if (uMsg != WIM_DATA) return;  //only "a buffer just filled up" is interesting here

	AudioCapture* self = reinterpret_cast<AudioCapture*>(dwInstance);
	WAVEHDR* header = reinterpret_cast<WAVEHDR*>(dwParam1);
	self->onBufferFull(header);
}

void AudioCapture::onBufferFull(WAVEHDR* header) {
	{
		std::lock_guard<std::mutex> lock(captureMutex);
		const int16_t* samples = reinterpret_cast<const int16_t*>(header->lpData);
		size_t sampleCount = header->dwBytesRecorded / sizeof(int16_t);
		capturedSamples.insert(capturedSamples.end(), samples, samples + sampleCount);
	}

	//Hand the same buffer straight back to the driver so recording keeps flowing while
	//whoever called start() decides when to stop(). Buffer-full callbacks must not block,
	//and re-adding an already-prepared buffer is cheap and documented-safe to do from here.
	if (recording.load() && waveInHandle) {
		waveInAddBuffer(waveInHandle, header, sizeof(WAVEHDR));
	}
}

void AudioCapture::playListeningCue() {
	Beep(1000, 120);   //short high beep - "I'm listening"
}

void AudioCapture::playProcessingCue() {
	Beep(600, 150);    //lower, slightly longer - distinct from the listening cue - "got it, thinking"
}

void AudioCapture::playNotReadyCue() {
	Beep(300, 100);    //two short low beeps - clearly distinct from the single-beep listening/processing cues
	Beep(300, 100);
}

void AudioCapture::playThinkingTickCue() {
	Beep(900, 35);     //quiet, brief tick - meant to repeat unobtrusively, not announce anything new
}

void AudioCapture::playDictateListeningCue() {
	//A quick double-blip, clearly distinct in RHYTHM (not just pitch) from playListeningCue()'s
	//single beep - "I'm listening, for dictation this time".
	Beep(1300, 60);
	Beep(1300, 60);
}

void AudioCapture::playDictateProcessingCue() {
	Beep(750, 150);    //distinct pitch from playProcessingCue()'s 600Hz - "got it, transcribing"
}
