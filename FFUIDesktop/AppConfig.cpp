#include "AppConfig.h"
#include <Windows.h>
#include <shlobj.h>
#include <fstream>

#pragma comment(lib, "shell32.lib")

std::wstring AppConfig::resolvePath() const {
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

	return settingsDir + L"\\ffui-config.ini";
}

void AppConfig::load() {
	std::wstring path = resolvePath();
	if (path.empty()) return;

	std::wifstream in(path);
	if (!in.is_open()) return;  //no config yet - every getter's own default applies, same spirit as every other settings file in this codebase

	std::wstring line;
	while (std::getline(in, line)) {
		size_t eq = line.find(L'=');
		if (eq == std::wstring::npos) continue;

		std::wstring key = line.substr(0, eq);
		std::wstring value = line.substr(eq + 1);
		//Strip a trailing \r, in case this file is ever hand-edited/copied from a \r\n source -
		//same defensive handling as every other .ini reader in this codebase.
		if (!value.empty() && value.back() == L'\r') value.pop_back();

		values[key] = value;
	}
}

void AppConfig::save() const {
	std::wstring path = resolvePath();
	if (path.empty()) return;

	std::wofstream out(path, std::ios::trunc);
	if (!out.is_open()) return;

	//std::map keeps keys sorted, so the file's own on-disk order is stable/alphabetical across
	//rewrites - purely a readability nicety for anyone hand-inspecting the file, not required for
	//correctness (load() doesn't care about order).
	for (const auto& pair : values) {
		out << pair.first << L"=" << pair.second << L"\n";
	}
}

bool AppConfig::hasKey(const std::wstring& key) const {
	return values.find(key) != values.end();
}

std::wstring AppConfig::getString(const std::wstring& key, const std::wstring& defaultValue) const {
	auto it = values.find(key);
	return it != values.end() ? it->second : defaultValue;
}

int AppConfig::getInt(const std::wstring& key, int defaultValue) const {
	auto it = values.find(key);
	if (it == values.end()) return defaultValue;
	try { return std::stoi(it->second); }
	catch (...) { return defaultValue; }
}

bool AppConfig::getBool(const std::wstring& key, bool defaultValue) const {
	auto it = values.find(key);
	if (it == values.end()) return defaultValue;
	return it->second == L"1" || it->second == L"true";
}

void AppConfig::setString(const std::wstring& key, const std::wstring& value) {
	values[key] = value;
	save();
}

void AppConfig::setInt(const std::wstring& key, int value) {
	values[key] = std::to_wstring(value);
	save();
}

void AppConfig::setBool(const std::wstring& key, bool value) {
	values[key] = value ? L"1" : L"0";
	save();
}
