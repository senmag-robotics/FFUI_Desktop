#include "VoiceSettingsManager.h"
#include "FFUIDesktop.h"   //for the global pSapiVoice / pAssistantVoice this file applies settings to
#include "AppConfig.h"     //the consolidated settings store this class's own values now live in
#include <Windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "shell32.lib")

std::vector<VoiceChoice> VoiceSettingsManager::enumerateVoices() {
	std::vector<VoiceChoice> result;

	ISpObjectTokenCategory* pVoiceCategory = nullptr;
	if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
		IID_ISpObjectTokenCategory, (void**)&pVoiceCategory)) || !pVoiceCategory) {
		return result;
	}

	if (SUCCEEDED(pVoiceCategory->SetId(SPCAT_VOICES, FALSE))) {
		IEnumSpObjectTokens* pVoiceEnum = nullptr;
		if (SUCCEEDED(pVoiceCategory->EnumTokens(NULL, NULL, &pVoiceEnum)) && pVoiceEnum) {
			ISpObjectToken* pToken = nullptr;
			while (pVoiceEnum->Next(1, &pToken, NULL) == S_OK && pToken) {
				VoiceChoice choice;

				LPWSTR idStr = nullptr;
				if (SUCCEEDED(pToken->GetId(&idStr)) && idStr) {
					choice.tokenId = idStr;
					CoTaskMemFree(idStr);
				}

				LPWSTR descStr = nullptr;
				//Empty attribute name ("") fetches the token's own default (friendly) name -
				//standard SAPI convention for voice tokens registered under SPCAT_VOICES.
				if (SUCCEEDED(pToken->GetStringValue(NULL, &descStr)) && descStr) {
					choice.displayName = descStr;
					CoTaskMemFree(descStr);
				}
				if (choice.displayName.empty()) {
					choice.displayName = choice.tokenId.empty() ? L"Unknown voice" : choice.tokenId;
				}

				if (!choice.tokenId.empty()) {
					result.push_back(choice);
				}

				pToken->Release();
				pToken = nullptr;
			}
			pVoiceEnum->Release();
		}
	}
	pVoiceCategory->Release();

	return result;
}

void VoiceSettingsManager::applyVoiceById(ISpVoice* voice, const std::wstring& tokenId) {
	if (!voice || tokenId.empty()) return;

	ISpObjectTokenCategory* pVoiceCategory = nullptr;
	if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
		IID_ISpObjectTokenCategory, (void**)&pVoiceCategory)) || !pVoiceCategory) {
		return;
	}

	if (SUCCEEDED(pVoiceCategory->SetId(SPCAT_VOICES, FALSE))) {
		IEnumSpObjectTokens* pVoiceEnum = nullptr;
		if (SUCCEEDED(pVoiceCategory->EnumTokens(NULL, NULL, &pVoiceEnum)) && pVoiceEnum) {
			ISpObjectToken* pToken = nullptr;
			while (pVoiceEnum->Next(1, &pToken, NULL) == S_OK && pToken) {
				LPWSTR idStr = nullptr;
				if (SUCCEEDED(pToken->GetId(&idStr)) && idStr) {
					if (tokenId == idStr) {
						voice->SetVoice(pToken);
						CoTaskMemFree(idStr);
						pToken->Release();
						break;
					}
					CoTaskMemFree(idStr);
				}
				pToken->Release();
				pToken = nullptr;
			}
			pVoiceEnum->Release();
		}
	}
	pVoiceCategory->Release();
}

void VoiceSettingsManager::applyRate(ISpVoice* voice, int rate) {
	if (!voice) return;
	if (rate < -10) rate = -10;
	if (rate > 10) rate = 10;
	voice->SetRate(rate);
}

void VoiceSettingsManager::setFfuiVoice(const std::wstring& tokenId) {
	ffuiVoiceId = tokenId;
	applyVoiceById(pSapiVoice, ffuiVoiceId);
	saveToDisk();
}

void VoiceSettingsManager::setFfuiRate(int rate) {
	if (rate < -10) rate = -10;
	if (rate > 10) rate = 10;
	ffuiRate = rate;
	applyRate(pSapiVoice, ffuiRate);
	saveToDisk();
}

void VoiceSettingsManager::setAssistantVoice(const std::wstring& tokenId) {
	assistantVoiceId = tokenId;
	applyVoiceById(pAssistantVoice, assistantVoiceId);
	saveToDisk();
}

void VoiceSettingsManager::setAssistantRate(int rate) {
	if (rate < -10) rate = -10;
	if (rate > 10) rate = 10;
	assistantRate = rate;
	applyRate(pAssistantVoice, assistantRate);
	saveToDisk();
}

//Resolves the OLD, pre-consolidation voice_settings.ini path - kept only for
//loadFromDiskAndApply()'s one-time migration read below, now that saveToDisk() itself writes
//through AppConfig instead. Unchanged from before this file existed.
std::wstring VoiceSettingsManager::resolveSettingsPath() {
	wchar_t* localAppData = nullptr;
	size_t envLen = 0;
	std::wstring settingsDir;
	if (_wdupenv_s(&localAppData, &envLen, L"LOCALAPPDATA") == 0 && localAppData) {
		settingsDir = std::wstring(localAppData) + L"\\FFUIDesktop";
	}
	if (localAppData) free(localAppData);
	if (settingsDir.empty()) return L"";  //shouldn't happen - %LOCALAPPDATA% is always set

	//Same "either success or already-exists is fine" handling as AssistantSetup.cpp's own
	//SHCreateDirectoryExW call for its whisper directory.
	int createResult = SHCreateDirectoryExW(NULL, settingsDir.c_str(), NULL);
	if (createResult != ERROR_SUCCESS && createResult != ERROR_ALREADY_EXISTS && createResult != ERROR_FILE_EXISTS) {
		return L"";
	}

	return settingsDir + L"\\voice_settings.ini";
}

//Writes through AppConfig now (see AppConfig.h's own comment for why) rather than this class's
//own private file - "ffui_voice_id"/etc keys become "voice.ffui_voice_id"/etc there, namespaced
//so they can't collide with QuickSlotsSettingsManager's or the tutorial's own keys in the same
//shared file.
void VoiceSettingsManager::saveToDisk() {
	AppConfig& config = AppConfig::getInstance();
	config.setString(L"voice.ffui_voice_id", ffuiVoiceId);
	config.setInt(L"voice.ffui_rate", ffuiRate);
	config.setString(L"voice.assistant_voice_id", assistantVoiceId);
	config.setInt(L"voice.assistant_rate", assistantRate);
}

void VoiceSettingsManager::loadFromDiskAndApply() {
	AppConfig& config = AppConfig::getInstance();

	//"voice.ffui_rate" absent means this machine has never saved through AppConfig yet - either a
	//genuine first run, or an upgrade from before this consolidation existed. Check the OLD
	//per-feature file exactly once in that case, so an existing user's already-saved voice
	//choice/rate isn't silently reset by this change - "lets make sure all the persistent
	//settings are written into one ffui-config file", not "starts everyone over".
	if (!config.hasKey(L"voice.ffui_rate")) {
		std::wstring oldPath = resolveSettingsPath();
		if (!oldPath.empty()) {
			std::wifstream in(oldPath);
			if (in.is_open()) {
				std::wstring line;
				while (std::getline(in, line)) {
					size_t eq = line.find(L'=');
					if (eq == std::wstring::npos) continue;

					std::wstring key = line.substr(0, eq);
					std::wstring value = line.substr(eq + 1);
					if (!value.empty() && value.back() == L'\r') value.pop_back();

					if (key == L"ffui_voice_id") ffuiVoiceId = value;
					else if (key == L"ffui_rate") { try { ffuiRate = std::stoi(value); } catch (...) {} }
					else if (key == L"assistant_voice_id") assistantVoiceId = value;
					else if (key == L"assistant_rate") { try { assistantRate = std::stoi(value); } catch (...) {} }
				}
			}
		}
		//Writes into the new consolidated file right away, migrated or not (a genuine first run
		//just persists today's defaults) - so this branch is only ever taken once per machine.
		saveToDisk();
	}
	else {
		ffuiVoiceId = config.getString(L"voice.ffui_voice_id");
		ffuiRate = config.getInt(L"voice.ffui_rate", 0);
		assistantVoiceId = config.getString(L"voice.assistant_voice_id");
		assistantRate = config.getInt(L"voice.assistant_rate", 0);
	}

	if (!ffuiVoiceId.empty()) applyVoiceById(pSapiVoice, ffuiVoiceId);
	applyRate(pSapiVoice, ffuiRate);

	if (!assistantVoiceId.empty()) applyVoiceById(pAssistantVoice, assistantVoiceId);
	applyRate(pAssistantVoice, assistantRate);
}
