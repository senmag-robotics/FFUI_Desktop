#pragma once

#include <windows.h>
#include <sapi.h>
#include <shlwapi.h>
#include <vector>
#include <cstdint>
#include <string>

//SHCreateMemStream lives in shlwapi.lib. SAPI's own interfaces/CLSIDs (ISpStream,
//ISpRecognizer, CLSID_SpInprocRecognizer, SPDFID_WaveFormatEx, ...) resolve the same way
//CLSID_SpVoice/IID_ISpVoice already do elsewhere in this project - no extra pragma needed.
#pragma comment(lib, "shlwapi.lib")

//Turns a short, already-captured block of 16kHz mono 16-bit PCM into text using Windows'
//built-in (SAPI) speech recognition. This recognizes against a finite in-memory buffer, not
//a live/continuous microphone stream - AudioCapture already did the "record while a button
//is held" part, so by the time recognize() runs there is nothing left to stream live.
//
//This is the single most fragile piece of the voice assistant feature (COM object lifetime,
//event-driven waiting, and the accuracy of the built-in engine itself are all real risks -
//see the project's voice-assistant plan for the full list) and is worth testing standalone,
//with a short pre-recorded clip in this class's PCM format, before relying on it end-to-end.
//It also needs a speech recognition language pack enabled on the machine (Settings > Time &
//Language > Speech) - nothing in this class checks for that; an absent language pack shows
//up here simply as recognize() always returning an empty string.
class SpeechRecognizer {
public:
	//Blocks until recognition completes or times out. sampleRateHz must match what pcm was
	//captured at (AudioCapture::SAMPLE_RATE_HZ). Returns an empty string on failure, timeout,
	//or "didn't catch any speech" - callers should treat all of those the same way (ask the
	//user to try again) rather than as a hard error.
	//
	//Call from the voice worker thread only, never the haptic thread - this does COM calls
	//and can block for seconds.
	std::wstring recognize(const std::vector<int16_t>& pcm, int sampleRateHz);

	//Checks whether SAPI has an installed, registered recognizer with English language support
	//available on this machine - a plain token-category enumeration query, no capture or
	//recognition involved, so it's fast (well under a second) and cheap to call. Used by the
	//self-configuring setup flow (AssistantSetup) to decide whether English speech recognition
	//needs to be provisioned before the assistant can be marked ready.
	//
	//"Language=409" is SAPI's standard hex-LCID attribute filter for English (US) - confirmed
	//against the real sapi.idl (ISpObjectTokenCategory::EnumTokens's pszReqAttribs parameter)
	//before writing this, same rigor already applied to the rest of this class.
	//
	//Call from the voice worker thread only, same as recognize() - COM calls, not haptic-safe.
	bool isEnglishRecognizerAvailable();

private:
	static constexpr DWORD RECOGNITION_TIMEOUT_MS = 8000;  //generous - dictation-from-stream can lag behind
};
