#include "WhisperTranscriber.h"
#include "ClaudeCliClient.h"   //for utf8ToWide() - reused rather than duplicating the same WinAPI conversion
#include <fstream>
#include <vector>

//Canonical 44-byte PCM WAV header. #pragma pack(1) is required - without it, the compiler is
//free to pad members for alignment, and this struct's raw bytes are written straight to disk as
//the file header, so any padding would corrupt it.
#pragma pack(push, 1)
struct WavHeader {
	char     riff[4] = { 'R','I','F','F' };
	uint32_t chunkSize = 0;         //36 + dataSize, filled in per-call below
	char     wave[4] = { 'W','A','V','E' };
	char     fmt[4] = { 'f','m','t',' ' };
	uint32_t fmtSize = 16;
	uint16_t audioFormat = 1;       //1 = PCM
	uint16_t numChannels = 1;       //mono - matches AudioCapture's capture format
	uint32_t sampleRate = 16000;    //overwritten per-call from the caller's sampleRateHz
	uint32_t byteRate = 0;          //sampleRate * numChannels * bitsPerSample/8, filled in below
	uint16_t blockAlign = 0;        //numChannels * bitsPerSample/8, filled in below
	uint16_t bitsPerSample = 16;
	char     data[4] = { 'd','a','t','a' };
	uint32_t dataSize = 0;          //filled in per-call below
};
#pragma pack(pop)

void WhisperTranscriber::setPaths(const std::wstring& exe, const std::wstring& model) {
	exePath = exe;
	modelPath = model;
}

std::wstring WhisperTranscriber::transcribe(const std::vector<int16_t>& pcm, int sampleRateHz) {
	std::wstring recognizedText;
	if (pcm.empty() || exePath.empty() || modelPath.empty()) return recognizedText;

	wchar_t tempDirBuf[MAX_PATH];
	if (GetTempPathW(MAX_PATH, tempDirBuf) == 0) return recognizedText;
	std::wstring tempDir = tempDirBuf;

	//Fixed filenames, not per-call-unique ones - this class's own contract (see the header) is
	//worker-thread-only, one call in flight at a time, so nothing else can collide with these
	//between writing the WAV and reading the output text back.
	std::wstring wavPath = tempDir + L"ffui_whisper_input.wav";
	std::wstring outBase = tempDir + L"ffui_whisper_output";
	std::wstring outTxtPath = outBase + L".txt";

	{
		WavHeader header{};
		uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
		header.sampleRate = static_cast<uint32_t>(sampleRateHz);
		header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
		header.byteRate = header.sampleRate * header.blockAlign;
		header.dataSize = dataBytes;
		header.chunkSize = 36 + dataBytes;

		std::ofstream wavFile(wavPath, std::ios::binary);
		if (!wavFile) return recognizedText;
		wavFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
		wavFile.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
	}

	//Clear out any stale output from a previous call before running. whisper-cli overwrites its
	//own output file on success, but deleting first makes "no output file afterward" an
	//unambiguous failure signal rather than possibly reading leftover text from an earlier,
	//unrelated transcription if this run fails silently.
	DeleteFileW(outTxtPath.c_str());

	//-otxt/-of: write plain text to outBase + ".txt". -nt: no timestamps in that text. -np: no
	//console output (we don't read stdout, just the file). -l en: pin English rather than
	//relying on Whisper's own language auto-detection, matching this project's English-only
	//design everywhere else (see AssistantSetup's English-only speech provisioning it replaced).
	std::wstring cmd = L"\"" + exePath + L"\" -m \"" + modelPath + L"\" -f \"" + wavPath +
		L"\" -otxt -of \"" + outBase + L"\" -nt -np -l en";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back(L'\0');

	BOOL created = CreateProcessW(NULL, mutableCmd.data(), NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

	if (created) {
		if (WaitForSingleObject(pi.hProcess, TRANSCRIBE_TIMEOUT_MS) != WAIT_OBJECT_0) {
			TerminateProcess(pi.hProcess, 1);
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		std::ifstream outFile(outTxtPath, std::ios::binary);
		if (outFile) {
			std::string utf8Text((std::istreambuf_iterator<char>(outFile)), std::istreambuf_iterator<char>());
			//whisper-cli's plain-text output is typically UTF-8 with a trailing newline (and
			//sometimes leading whitespace, depending on the model's leading token) - trim both
			//ends rather than passing raw whitespace through to speech recognition/Claude.
			size_t start = utf8Text.find_first_not_of(" \t\r\n");
			size_t end = utf8Text.find_last_not_of(" \t\r\n");
			if (start != std::string::npos) {
				utf8Text = utf8Text.substr(start, end - start + 1);
				recognizedText = ClaudeCliClient::utf8ToWide(utf8Text);
			}
		}
	}

	DeleteFileW(wavPath.c_str());
	DeleteFileW(outTxtPath.c_str());

	return recognizedText;
}
