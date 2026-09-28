#pragma once
#include <string>

//Owns persistence and application of the user-configurable "how many quickslots" setting - "add
//a user setting (a horizontal scroll in the FFUI settings) for how many quickslots slots they
//want", per the request. A small sibling to VoiceSettingsManager (same singleton pattern, same
//%LOCALAPPDATA%\FFUIDesktop\ directory, same hand-rolled key=value .ini persistence - see that
//class's own header comment for why that format was chosen over anything else) rather than
//folding into it - voice settings and the quickslot count are unrelated domains, and every other
//cohesive concern in this codebase already owns its own singleton (WindowManager, MenuSystem,
//VoiceSettingsManager, VoiceAssistant, DeviceManager).
//
//A Meyer's singleton, matching VoiceSettingsManager/WindowManager's own pattern.
class QuickSlotsSettingsManager {
private:
	QuickSlotsSettingsManager() {}
public:
	static QuickSlotsSettingsManager& getInstance() {
		static QuickSlotsSettingsManager instance;
		return instance;
	}
	QuickSlotsSettingsManager(QuickSlotsSettingsManager const&) = delete;
	void operator=(QuickSlotsSettingsManager const&) = delete;

	//1-6: min=1 because there must always be at least one visible/current slot; max=6 mirrors
	//this app's own existing "6 rows" grid convention (ObjectFactory::computeGridCell's default
	//maxRows=6) rather than introducing a new, separate numeric convention.
	static constexpr int MIN_SLOT_COUNT = 1;
	static constexpr int MAX_SLOT_COUNT = 6;

	int getSlotCount() const { return slotCount; }

	//Clamps to [MIN_SLOT_COUNT, MAX_SLOT_COUNT], applies to WindowManager (via
	//WindowManager::setActiveSlotCount(), which also clamps currentActiveSlotIndex if it's now
	//out of range), and persists to disk. Called once, on release, from the Quick Slots menu's
	//Slider item's commitValue - see FFUIDesktop::openQuickSlotsMenu()'s own comment for why the
	//live setValue during the drag is deliberately a no-op instead.
	void setSlotCount(int count);

	//Loads quickslots_settings.ini (if present) and applies it to WindowManager - called once
	//from FFUIDesktop::initDesktop(), after WindowManager exists. A missing or unreadable file
	//is not an error - the count is simply left at its default (2, matching
	//WindowManager::numOfActiveWindows's own default), exactly today's pre-feature behavior.
	void loadFromDiskAndApply();

private:
	int slotCount = 2;   //matches WindowManager::numOfActiveWindows's own default

	//Resolves %LOCALAPPDATA%\FFUIDesktop\quickslots_settings.ini, creating the FFUIDesktop
	//directory if it doesn't exist yet - identical pattern to
	//VoiceSettingsManager::resolveSettingsPath(). Returns an empty string (and creates nothing)
	//if %LOCALAPPDATA% itself can't be resolved - shouldn't happen on a real Windows install.
	std::wstring resolveSettingsPath();

	void saveToDisk();
};
