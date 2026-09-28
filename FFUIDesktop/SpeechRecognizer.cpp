#include "SpeechRecognizer.h"
#include <chrono>

std::wstring SpeechRecognizer::recognize(const std::vector<int16_t>& pcm, int sampleRateHz) {
	std::wstring recognizedText;

	if (pcm.empty()) return recognizedText;

	//Wrap the captured PCM in an in-memory IStream. SHCreateMemStream copies the data in, so
	//our vector is free to go out of scope once this function returns.
	IStream* pMemStream = SHCreateMemStream(
		reinterpret_cast<const BYTE*>(pcm.data()),
		static_cast<UINT>(pcm.size() * sizeof(int16_t)));
	if (!pMemStream) return recognizedText;

	WAVEFORMATEX wfx{};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 1;
	wfx.nSamplesPerSec = sampleRateHz;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.cbSize = 0;

	ISpStream* pSpStream = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_SpStream, NULL, CLSCTX_ALL, IID_ISpStream, (void**)&pSpStream);
	if (FAILED(hr) || !pSpStream) {
		pMemStream->Release();
		return recognizedText;
	}

	hr = pSpStream->SetBaseStream(pMemStream, SPDFID_WaveFormatEx, &wfx);
	pMemStream->Release();  //SetBaseStream AddRef'd it internally; we're done with our own reference
	if (FAILED(hr)) {
		pSpStream->Release();
		return recognizedText;
	}

	ISpRecognizer* pRecognizer = nullptr;
	hr = CoCreateInstance(CLSID_SpInprocRecognizer, NULL, CLSCTX_ALL, IID_ISpRecognizer, (void**)&pRecognizer);
	if (FAILED(hr) || !pRecognizer) {
		pSpStream->Release();
		return recognizedText;
	}

	//fAllowFormatChanges=TRUE: let the recognizer resample/convert if it prefers a different
	//internal format, rather than hard-failing just because it wants something other than
	//exactly 16kHz mono 16-bit.
	hr = pRecognizer->SetInput(pSpStream, TRUE);
	if (FAILED(hr)) {
		pRecognizer->Release();
		pSpStream->Release();
		return recognizedText;
	}

	ISpRecoContext* pRecoContext = nullptr;
	hr = pRecognizer->CreateRecoContext(&pRecoContext);
	if (FAILED(hr) || !pRecoContext) {
		pRecognizer->Release();
		pSpStream->Release();
		return recognizedText;
	}

	pRecoContext->SetNotifyWin32Event();
	pRecoContext->SetInterest(
		SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_END_SR_STREAM),
		SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_END_SR_STREAM));

	ISpRecoGrammar* pGrammar = nullptr;
	hr = pRecoContext->CreateGrammar(1, &pGrammar);
	if (FAILED(hr) || !pGrammar) {
		pRecoContext->Release();
		pRecognizer->Release();
		pSpStream->Release();
		return recognizedText;
	}

	hr = pGrammar->LoadDictation(NULL, SPLO_STATIC);
	if (SUCCEEDED(hr)) {
		pGrammar->SetDictationState(SPRS_ACTIVE);
	}

	//Pump events until either a recognition result arrives or the (finite) stream runs dry,
	//whichever happens first, bounded by a hard timeout so a misbehaving engine can't hang
	//the voice worker thread forever.
	HANDLE hNotifyEvent = pRecoContext->GetNotifyEventHandle();
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(RECOGNITION_TIMEOUT_MS);

	bool streamEnded = false;
	SPEVENT spEvent{};
	ULONG fetched = 0;

	while (recognizedText.empty() && !streamEnded) {
		auto now = std::chrono::steady_clock::now();
		if (now >= deadline) break;

		DWORD waitMs = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
		if (WaitForSingleObject(hNotifyEvent, waitMs) != WAIT_OBJECT_0) break;  //timeout or wait error

		while (recognizedText.empty() && !streamEnded &&
			SUCCEEDED(pRecoContext->GetEvents(1, &spEvent, &fetched)) && fetched == 1) {

			if (spEvent.eEventId == SPEI_RECOGNITION && spEvent.elParamType == SPET_LPARAM_IS_OBJECT) {
				ISpRecoResult* pResult = reinterpret_cast<ISpRecoResult*>(spEvent.lParam);
				if (pResult) {
					LPWSTR pszText = nullptr;
					if (SUCCEEDED(pResult->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &pszText, NULL)) && pszText) {
						recognizedText = pszText;
						CoTaskMemFree(pszText);
					}
					pResult->Release();  //the event handed us a ref via lParam - we own releasing it
				}
			}
			else if (spEvent.eEventId == SPEI_END_SR_STREAM) {
				streamEnded = true;
			}
		}
	}

	pGrammar->Release();
	pRecoContext->Release();
	pRecognizer->Release();
	pSpStream->Release();

	return recognizedText;
}

bool SpeechRecognizer::isEnglishRecognizerAvailable() {
	bool available = false;

	ISpObjectTokenCategory* pCategory = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
		IID_ISpObjectTokenCategory, (void**)&pCategory);
	if (FAILED(hr) || !pCategory) return false;

	//fCreateIfNotExist=FALSE: if the recognizers category doesn't exist at all (speech
	//recognition has never been touched on this machine), SetId fails and we fall straight
	//through to "not available" - exactly the right answer, since there's nothing to enumerate.
	hr = pCategory->SetId(SPCAT_RECOGNIZERS, FALSE);
	if (SUCCEEDED(hr)) {
		IEnumSpObjectTokens* pEnum = nullptr;
		hr = pCategory->EnumTokens(L"Language=409", NULL, &pEnum);
		if (SUCCEEDED(hr) && pEnum) {
			ULONG count = 0;
			if (SUCCEEDED(pEnum->GetCount(&count)) && count > 0) {
				available = true;
			}
			pEnum->Release();
		}
	}

	pCategory->Release();
	return available;
}
