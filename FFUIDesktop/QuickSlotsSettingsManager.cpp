#include "QuickSlotsSettingsManager.h"
#include "WindowWallObject.h"   //for WindowManager::setActiveSlotCount()
#include "AppConfig.h"          //the consolidated settings store this class's own value now lives in
#include <Windows.h>
#include <shlobj.h>
#include <fstream>

#pragma comment(lib, "shell32.lib")

void QuickSlotsSettingsManager::setSlotCount(int count) {
	if (count < MIN_SLOT_COUNT) count = MIN_SLOT_COUNT;
	if (count > MAX_SLOT_COUNT) count = MAX_SLOT_COUNT;

	slotCount = count;
	WindowManager::getInstance().setActiveSlotCount(slotCount);
	saveToDisk();
}

//Resolves the OLD, pre-consolidation quickslots_settings.ini path - kept only for
//loadFromDiskAndApply()'s one-time migration read below, now that saveToDisk() itself writes
//through AppConfig instead. Unchanged from before this file existed.
std::wstring QuickSlotsSettingsManager::resolveSettingsPath() {
	wchar_t* localAppData = nullptr;
	size_t envLen = 0;
	std::wstring settingsDir;
	if (_wdupenv_s(&localAppData, &envLen, L"LOCALAPPDATA") == 0 && localAppData) {
		settingsDir = std::wstring(localAppData) + L"\\FFUIDesktop";
	}
	if (localAppData) free(localAppData);
	if (settingsDir.empty()) return L"";  //shouldn't happen - %LOCALAPPDATA% is always set

	//Same "either success or already-exists is fine" handling as
	//VoiceSettingsManager::resolveSettingsPath()'s own SHCreateDirectoryExW call.
	int createResult = SHCreateDirectoryExW(NULL, settingsDir.c_str(), NULL);
	if (createResult != ERROR_SUCCESS && createResult != ERROR_ALREADY_EXISTS && createResult != ERROR_FILE_EXISTS) {
		return L"";
	}

	return settingsDir + L"\\quickslots_settings.ini";
}

//Writes through AppConfig now (see AppConfig.h's own comment for why), under the "quickslots."
//prefix, rather than this class's own private file.
void QuickSlotsSettingsManager::saveToDisk() {
	AppConfig::getInstance().setInt(L"quickslots.slot_count", slotCount);
}

void QuickSlotsSettingsManager::loadFromDiskAndApply() {
	AppConfig& config = AppConfig::getInstance();

	//"quickslots.slot_count" absent means this machine has never saved through AppConfig yet -
	//check the OLD per-feature file exactly once, so an existing user's already-saved count isn't
	//silently reset to the default by this change - same one-time migration approach as
	//VoiceSettingsManager::loadFromDiskAndApply().
	if (!config.hasKey(L"quickslots.slot_count")) {
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

					if (key == L"slot_count") {
						try { slotCount = std::stoi(value); } catch (...) {}
					}
				}
			}
		}
		saveToDisk();
	}
	else {
		slotCount = config.getInt(L"quickslots.slot_count", 2);
	}

	if (slotCount < MIN_SLOT_COUNT) slotCount = MIN_SLOT_COUNT;
	if (slotCount > MAX_SLOT_COUNT) slotCount = MAX_SLOT_COUNT;

	WindowManager::getInstance().setActiveSlotCount(slotCount);
}
