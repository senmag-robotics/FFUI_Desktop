#pragma once
#include <sapi.h>
#include <string>
#include <vector>

//One entry from SPCAT_VOICES - a real, installed SAPI voice the user can pick between.
struct VoiceChoice {
	std::wstring tokenId;       //stable SAPI token ID string (e.g. "HKEY_LOCAL_MACHINE\...\Tokens\...") - safe to persist to disk, unlike a plain enumeration index which can reorder if voices are installed/removed.
	std::wstring displayName;   //the token's friendly name, e.g. "Microsoft David Desktop" - what gets spoken/narrated to the user.
};

//Owns persistence and application of the two independent narrator voices' settings (voice
//choice + speech rate), for both pSapiVoice (FFUI's own window/UI narrator) and pAssistantVoice
//(the AI assistant's narrator) - see their declarations in FFUIDesktop.h for why there are two.
//
//A Meyer's singleton, matching WindowManager's own pattern elsewhere in this codebase.
//
//Persists to a small hand-rolled key=value text file under
//%LOCALAPPDATA%\FFUIDesktop\voice_settings.ini - no JSON library exists anywhere in this
//codebase (confirmed while researching this feature), and the payload here is four flat,
//known fields, so a plain key=value format (one per line) is the simplest correct choice,
//matching the spirit of ClaudeCliClient's own hand-rolled JSON-field extractor elsewhere in
//this app. The %LOCALAPPDATA%\FFUIDesktop\... path convention itself is lifted directly from
//AssistantSetup.cpp's own whisper-model directory (see loadFromDisk()/saveToDisk() for the
//exact _wdupenv_s(.... L"LOCALAPPDATA") pattern reused here).
class VoiceSettingsManager {
private:
	VoiceSettingsManager() {}
public:
	static VoiceSettingsManager& getInstance() {
		static VoiceSettingsManager instance;
		return instance;
	}
	VoiceSettingsManager(VoiceSettingsManager const&) = delete;
	void operator=(VoiceSettingsManager const&) = delete;

	//Enumerates every voice currently installed on this machine via SAPI's own token
	//enumeration (SPCAT_VOICES) - the same category-enumeration pattern this codebase already
	//uses in FFUIDesktop::initDesktop() (there, just to pick a second voice for
	//pAssistantVoice) and in SpeechRecognizer's SPCAT_RECOGNIZERS check. Returns an empty
	//vector (never throws) if SAPI enumeration fails for any reason.
	std::vector<VoiceChoice> enumerateVoices();

	//Applies tokenId to the given voice (pSapiVoice or pAssistantVoice) by re-enumerating
	//SPCAT_VOICES and matching on GetId() - deliberately not using the ATL-based
	//SpGetTokenFromId() helper (sphelper.h), which nothing else in this codebase includes;
	//this keeps voice lookup consistent with the raw-COM enumeration style already
	//established here. No-ops (does not clear the voice) if tokenId isn't found - e.g. a
	//voice that was uninstalled since it was last saved to disk.
	void applyVoiceById(ISpVoice* voice, const std::wstring& tokenId);

	//Rate is a plain ISpVoice::SetRate()/GetRate() call, no lookup needed - range is -10..10
	//per SAPI's own documented ISpVoice::SetRate() contract, clamped defensively here too.
	void applyRate(ISpVoice* voice, int rate);

	//--- FFUI narrator (pSapiVoice) ---
	void setFfuiVoice(const std::wstring& tokenId);
	void setFfuiRate(int rate);
	const std::wstring& getFfuiVoiceId() const { return ffuiVoiceId; }
	int getFfuiRate() const { return ffuiRate; }

	//--- AI assistant narrator (pAssistantVoice) ---
	void setAssistantVoice(const std::wstring& tokenId);
	void setAssistantRate(int rate);
	const std::wstring& getAssistantVoiceId() const { return assistantVoiceId; }
	int getAssistantRate() const { return assistantRate; }

	//Loads voice_settings.ini (if present) into the four fields above, then applies them to
	//pSapiVoice/pAssistantVoice - called once from FFUIDesktop::initDesktop(), after both
	//voices exist. A missing or unreadable file is not an error - both voices are simply left
	//on SAPI's own defaults (exactly today's pre-feature behavior), matching the "empty/
	//first-run" case rather than failing startup.
	void loadFromDiskAndApply();

private:
	std::wstring ffuiVoiceId;       //empty = "use SAPI's default voice", never explicitly chosen
	int ffuiRate = 0;
	std::wstring assistantVoiceId;
	int assistantRate = 0;

	//Resolves %LOCALAPPDATA%\FFUIDesktop\voice_settings.ini, creating the FFUIDesktop
	//directory if it doesn't exist yet. Returns an empty string (and creates nothing) if
	//%LOCALAPPDATA% itself can't be resolved - shouldn't happen on a real Windows install.
	std::wstring resolveSettingsPath();

	void saveToDisk();
};
