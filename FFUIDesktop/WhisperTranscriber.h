#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

//Transcribes captured audio to text via whisper.cpp's whisper-cli.exe, spawned as a subprocess
//per utterance - the same CreateProcess-and-wait pattern ClaudeCliClient already uses for the
//`claude` CLI, rather than linking whisper.cpp's library directly and calling its C API in
//process. Chosen over linking because this project has no way to compile/verify a from-scratch
//native binding in the sandbox it was built in - the subprocess route reuses an
//already-proven pattern in this exact codebase instead of a guess at an unverified one. If the
//per-call process-spawn/model-load latency ever proves to be a real problem in practice, linking
//whisper.cpp directly (keeping the model resident in memory across calls) is the natural
//follow-up, not attempted here.
//
//Swapped in to replace SpeechRecognizer (SAPI's built-in recognizer) once real-world testing
//showed SAPI's recognition accuracy too weak to rely on - see the project's voice-assistant
//plan. SpeechRecognizer.h/.cpp are left in the project unused, in case of a fallback or direct
//comparison later, but nothing calls into them any more.
//
//whisper-cli.exe and its model file are provisioned by AssistantSetup (auto-downloaded from the
//official ggml-org/whisper.cpp GitHub releases and the model's official Hugging Face home under
//the whisper.cpp maintainer's own account, respectively - see
//AssistantSetup::ensureWhisperReady()) rather than bundled with FFUI itself, mirroring how the
//Claude CLI is resolved/installed rather than bundled.
class WhisperTranscriber {
public:
	//Called once by AssistantSetup after ensureWhisperReady() resolves (or freshly downloads)
	//both files - mirrors ClaudeCliClient::setCliPath(). Both must be set before the first
	//transcribe() call; transcribe() fails harmlessly (returns empty) if either is still empty.
	void setPaths(const std::wstring& exePath, const std::wstring& modelPath);

	//Blocks until transcription completes or times out. sampleRateHz must match what pcm was
	//captured at - AudioCapture::SAMPLE_RATE_HZ is 16kHz mono, which is also exactly what
	//whisper.cpp's own (ffmpeg-less) WAV reader and the ggml English models expect, so no
	//resampling is ever needed here; this just wraps the raw PCM in a standard WAV header.
	//Returns an empty string on failure/timeout/"didn't catch any speech" - same contract
	//SpeechRecognizer::recognize() used, so callers treat all of those the same way (ask the
	//user to try again) rather than as a hard error.
	//
	//Call from the voice worker thread only, never the haptic thread - spawns a process, writes
	//a temp file, and can block for several seconds of CPU-only inference.
	std::wstring transcribe(const std::vector<int16_t>& pcm, int sampleRateHz);

private:
	std::wstring exePath;
	std::wstring modelPath;

	//Generous - CPU-only base.en inference on a several-second utterance plus process spawn
	//should be well under this, but padded for a cold-cache first run after boot. Same
	//"generous but bounded" spirit as RECOGNITION_TIMEOUT_MS in the SAPI-based SpeechRecognizer
	//this replaces.
	static constexpr DWORD TRANSCRIBE_TIMEOUT_MS = 15000;
};
