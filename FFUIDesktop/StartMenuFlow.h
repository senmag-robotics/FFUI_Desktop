#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include "ProgramLauncher.h"

class VoiceAssistant;   //only used as a reference parameter here - see update()'s own comment

//Drives the "Start programs" haptic flow: select the tile -> dictate a search term -> search
//installed programs -> present up to 3 matches (+ retry/cancel) as a haptic menu -> launch the
//selected program, assign it to whichever quickslot was last active, and switch to it. See the
//request: "The start menu will need to function as a way for the user to launch programs...
//prompting the user to dictate a search term, then search through installed programs... present
//up to 3 matches (+ a retry / cancel options) as a haptic menu. If a program is selected, launch
//it, and assign it to whichever slot was used last, then activate that slot".
//
//A Meyer's singleton, matching WindowManager/MenuSystem/VoiceSettingsManager/
//QuickSlotsSettingsManager's own pattern. Entirely haptic-thread-owned for its OWN state
//(begin()/update()/state() - only ever called from FFUIDesktop::updateFrame(), same as
//MenuSystem itself, so no locking is needed for state_ specifically) - the actual blocking work
//(the program search, and launching+detecting a new window) runs on short-lived background
//std::jthreads this class spawns itself (searchThread/launchThread below), each doing its own
//CoInitializeEx/CoUninitialize, mirroring VoiceAssistant::workerLoop()'s own COM-per-thread
//pattern - NOT VoiceAssistant's persistent worker thread, since piggy-backing this feature's
//filesystem/process-launch work onto that already-delicate AudioCapture/WhisperTranscriber-
//owning loop would risk destabilizing it for no real benefit; a plain jthread reproduces the
//same "blocking work off the haptic thread, safe to speak/touch WindowManager from" guarantee
//with far less coupling. Results cross back to the haptic thread via small mutex-guarded
//mailboxes, mirroring WindowManager::hasPendingExplicitFocus/pendingExplicitFocusHandle's own
//producer/consumer pattern.
class StartMenuFlow {
private:
    StartMenuFlow() {}
public:
    static StartMenuFlow& getInstance() {
        static StartMenuFlow instance;
        return instance;
    }
    StartMenuFlow(StartMenuFlow const&) = delete;
    void operator=(StartMenuFlow const&) = delete;

    enum class State {
        Idle,
        AwaitingDictation,   //prompt has been spoken; waiting on the user to hold AUX and speak
        Searching,           //dictation captured; ProgramLauncher::search() running in the background
        ShowingResults       //a MenuSystem menu (matches + Try again + Cancel) is open
    };

    State state() const { return state_; }

    //Called when the "Start programs" tile is selected (FFUIDesktop::openSettingsTileMenu()'s
    //tileIndex==0 branch) - narrates the dictation prompt via pSapiVoice (this is FFUI's own
    //narrator prompting, not the AI assistant) and enters AwaitingDictation. Deliberately does
    //NOT open a MenuSystem menu yet - there's nothing to select until search results exist.
    void begin();

    //Called once every haptic frame from FFUIDesktop::updateFrame(), regardless of state (a
    //no-op while Idle or ShowingResults - see its own body). While AwaitingDictation, polls
    //voiceAssistant's dictation mailbox (VoiceAssistant::tryConsumePendingDictation()) - passed
    //in by reference rather than reached for as a singleton, since VoiceAssistant is a plain
    //FFUIDesktop member, not a Meyer's singleton. While Searching, polls the search-results
    //mailbox and, once ready, builds and opens the results menu. While ShowingResults, only
    //resets state_ back to Idle once the menu it opened has actually closed (Cancel, or any
    //other way MenuSystem might close it) - the menu's own Action/Back items handle the normal
    //"user picked something" transitions themselves (see showResults()'s own comment).
    void update(VoiceAssistant& voiceAssistant);

private:
    State state_ = State::Idle;
    std::wstring lastQuery;   //stashed for "No matches found for '{query}'" narration

    void beginSearch(const std::wstring& query);
    void showResults(const std::vector<ProgramLauncher::InstalledProgram>& results);
    void beginLaunch(const ProgramLauncher::InstalledProgram& program);

    std::jthread searchThread;
    std::jthread launchThread;

    std::mutex searchResultMutex;
    bool hasPendingSearchResults = false;
    std::vector<ProgramLauncher::InstalledProgram> pendingSearchResults;
};
