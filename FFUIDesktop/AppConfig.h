#pragma once
#include <string>
#include <map>

//Single consolidated settings file for the whole app -
//%LOCALAPPDATA%\FFUIDesktop\ffui-config.ini - "lets make sure all the persistent settings are
//written into one ffui-config file on disk somewhere? (appdata or similar?)", per the request.
//Supersedes VoiceSettingsManager's own voice_settings.ini and QuickSlotsSettingsManager's own
//quickslots_settings.ini (both migrated to store their values here instead, under their own key
//prefix, with a one-time migration from their old separate files the first time this runs after
//upgrading - see each manager's own loadFromDiskAndApply() for exactly how), plus the tutorial's
//own "has this ever run before" flag (tutorial.hasCompletedTutorialOnce) that motivated this
//consolidation in the first place - see TutorialFlow.h.
//
//A Meyer's singleton, matching every other settings-owning class in this codebase
//(VoiceSettingsManager, QuickSlotsSettingsManager, WindowManager, MenuSystem, VoiceAssistant).
//Same hand-rolled key=value text format those two files already used individually (see
//VoiceSettingsManager.h's own comment for why - no JSON library exists anywhere in this
//codebase), just one shared file instead of one per concern, with keys namespaced by a
//"domain." prefix (e.g. "voice.ffui_rate", "quickslots.slot_count", "tutorial.hasCompletedTutorialOnce")
//so unrelated settings can't collide. Loaded once, lazily, the moment anything first calls
//getInstance() - earlier than VoiceSettingsManager/QuickSlotsSettingsManager's own explicit
//loadFromDiskAndApply() calls from FFUIDesktop::initDesktop(), which is fine since this class
//does nothing on construction beyond a cheap file read into memory.
class AppConfig {
private:
	AppConfig() { load(); }
public:
	static AppConfig& getInstance() {
		static AppConfig instance;
		return instance;
	}
	AppConfig(AppConfig const&) = delete;
	void operator=(AppConfig const&) = delete;

	std::wstring getString(const std::wstring& key, const std::wstring& defaultValue = L"") const;
	int getInt(const std::wstring& key, int defaultValue = 0) const;
	bool getBool(const std::wstring& key, bool defaultValue = false) const;

	//Every setter persists to disk immediately (a plain, infrequent text-file rewrite - none of
	//these are ever called anywhere near haptic-frame rate), matching VoiceSettingsManager/
	//QuickSlotsSettingsManager's own "every setter saves" convention, so callers never need to
	//remember to flush separately.
	void setString(const std::wstring& key, const std::wstring& value);
	void setInt(const std::wstring& key, int value);
	void setBool(const std::wstring& key, bool value);

	//True only if `key` has ever actually been written to THIS consolidated file - distinct from
	//"written with an empty/zero/false value", and false for a key that only exists in one of the
	//old, pre-consolidation per-feature files. Used by VoiceSettingsManager/
	//QuickSlotsSettingsManager's own migration step to tell "never migrated yet - go check the old
	//file" apart from "already migrated (or a genuine first run - false either way looks the same
	//from here) - don't consult the old file again".
	bool hasKey(const std::wstring& key) const;

private:
	std::map<std::wstring, std::wstring> values;

	//Resolves %LOCALAPPDATA%\FFUIDesktop\ffui-config.ini, creating the FFUIDesktop directory if
	//it doesn't exist yet - identical pattern to VoiceSettingsManager::resolveSettingsPath() (see
	//that class's own comment), just a different final filename.
	std::wstring resolvePath() const;
	void load();
	void save() const;
};
