#include "StartMenuFlow.h"
#include "FFUIDesktop.h"      //for pSapiVoice - FFUI's own narrator, prompting/reporting for this flow
#include "VoiceAssistant.h"   //for tryConsumePendingDictation()
#include "MenuSystem.h"
#include "WindowWallObject.h" //for WindowManager::assignWindowToSlot()/loadSlot()/currentActiveSlotIndex
#include <windows.h>
#include <sapi.h>
#include <iostream>  //for mirroring narrator requests to the console - see each Speak() call site below

namespace {
    // Whisper's decoder routinely appends trailing sentence punctuation to even a single
    // dictated word - "excel." or "excel?" rather than plain "excel" - which is worse than
    // useless for ProgramLauncher::search()'s prefix/substring matching against installed
    // program names (an exact-looking "excel." never prefix-matches "Excel"). Strips
    // leading/trailing whitespace and a small set of common punctuation marks from both ends of
    // the recognized text before it's used as a search term or narrated back - deliberately NOT
    // touching punctuation in the INTERIOR of the string, since a program name could
    // legitimately contain some (e.g. "M&M's").
    std::wstring sanitizeDictatedQuery(const std::wstring& raw) {
        static const std::wstring trimChars = L" \t\r\n.,!?;:\"'";
        size_t start = raw.find_first_not_of(trimChars);
        if (start == std::wstring::npos) return L"";
        size_t end = raw.find_last_not_of(trimChars);
        return raw.substr(start, end - start + 1);
    }
}

void StartMenuFlow::begin() {
    lastQuery.clear();
    {
        std::lock_guard<std::mutex> lock(searchResultMutex);
        hasPendingSearchResults = false;
        pendingSearchResults.clear();
    }

    state_ = State::AwaitingDictation;

    if (pSapiVoice) {
        // Mirrors every narrator request to the console, tagged by source - "mirror all the
        // narrator requests to the console please", per the request.
        std::cout << "[Narrate:StartMenu] \"Hold the Aux button and say a program name.\"" << std::endl;
        SpeakFfuiNarration(L"Hold the Aux button and say a program name.");
    }
}

void StartMenuFlow::update(VoiceAssistant& voiceAssistant) {
    if (state_ == State::Idle) {
        return;
    }

    if (state_ == State::AwaitingDictation) {
        std::wstring dictated;
        if (voiceAssistant.tryConsumePendingDictation(dictated)) {
            // Strip whisper's stray leading/trailing punctuation ("excel." -> "excel") before
            // it's used as a search term - see sanitizeDictatedQuery()'s own comment.
            std::wstring cleaned = sanitizeDictatedQuery(dictated);
            if (cleaned.empty()) {
                // Shouldn't normally happen - VoiceAssistant::runDictation() only fills the
                // mailbox on a non-empty transcription, and speaks its own "Sorry, I didn't
                // catch that" through pAssistantVoice otherwise (see its own comment) - but
                // stay in AwaitingDictation defensively rather than searching for nothing, in
                // case dictated turned out to be pure punctuation/whitespace.
                return;
            }
            lastQuery = cleaned;
            state_ = State::Searching;
            beginSearch(cleaned);
        }
        return;
    }

    if (state_ == State::Searching) {
        std::vector<ProgramLauncher::InstalledProgram> results;
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(searchResultMutex);
            if (hasPendingSearchResults) {
                results = pendingSearchResults;
                pendingSearchResults.clear();
                hasPendingSearchResults = false;
                ready = true;
            }
        }
        if (ready) {
            showResults(results);
        }
        return;
    }

    if (state_ == State::ShowingResults) {
        // The menu's own Action/Back items (see showResults() below) handle the normal "user
        // picked something" transitions themselves - this is only here to notice the menu
        // closing (Cancel, or any other way MenuSystem might close it) and fall back to Idle
        // so a stray future update() call doesn't keep re-checking a menu that's already gone.
        if (!MenuSystem::getInstance().isActive()) {
            state_ = State::Idle;
        }
        return;
    }
}

void StartMenuFlow::beginSearch(const std::wstring& query) {
    searchThread = std::jthread([this, query](std::stop_token stoken) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

        std::vector<ProgramLauncher::InstalledProgram> results = ProgramLauncher::search(query, 3);

        if (!stoken.stop_requested()) {
            std::lock_guard<std::mutex> lock(searchResultMutex);
            pendingSearchResults = results;
            hasPendingSearchResults = true;
        }

        CoUninitialize();
    });
}

void StartMenuFlow::showResults(const std::vector<ProgramLauncher::InstalledProgram>& results) {
    // Built here and handed to createMenu() as its spokenAnnouncement (below) rather than
    // spoken separately first - createMenu()'s own "Menu opened: ..." announcement uses
    // SPF_PURGEBEFORESPEAK, which would otherwise cut this off the instant it's issued (the
    // same clipping bug the drop-slot narration fix closed elsewhere - see
    // MenuSystem::createMenu()'s own comment).
    std::wstring announcement;
    if (results.empty()) {
        announcement = L"No matches found for " + lastQuery + L".";
    }
    else {
        announcement = L"Found " + std::to_wstring(results.size()) +
            (results.size() == 1 ? L" result for " : L" results for ") + lastQuery + L".";

        // New this round - "when finding multiple options, prompt the user to move up and down
        // to select between them": only said when there's actually a choice to make between
        // program matches (a single match still has Try again/Cancel below it, but there's
        // nothing to navigate BETWEEN yet at that point). "Up and down" is literally accurate for
        // this menu specifically - ObjectFactory::computeMenuCell() lays out every item in a
        // single column (row = index, one column) as long as the item count stays within its own
        // maxRows default of 6, which this menu (at most 3 results + Try again + Cancel = 5)
        // always does.
        if (results.size() > 1) {
            announcement += L" Move up and down to select.";
        }
    }

    std::vector<MenuItemDef> items;

    for (const ProgramLauncher::InstalledProgram& program : results) {
        MenuItemDef item;
        item.label = program.displayName;
        item.type = MenuItemType::Action;
        item.onSelect = [this, program]() {
            // MenuSystem::selectHighlighted() calls this through a reference into the very
            // items vector MenuSystem::closeAll() below clears - that destroys THIS closure's
            // own backing storage (it lives inside the MenuItemDef being cleared), so anything
            // captured here (this, program) can no longer be safely read once closeAll() has
            // returned. closeAll() must therefore run LAST, after everything that needs `this`/
            // `program` - reordering this the other way around caused a real crash (read access
            // violation dereferencing `this`) once this path actually started firing.
            //
            // New this round - "when an option is selected, read a confirmation 'Confirmed,
            // Launching <program name> now'". Spoken here, synchronously, before closeAll() runs
            // (not after - `program` is no longer safely readable once closeAll() has destroyed
            // this closure's own backing storage, same ordering constraint as everything else in
            // this callback). Sets suppressNextMenuCloseRenarration (FFUIDesktop.h) right after,
            // so the room's own "moved to a new slot" re-narration - which closeAll() below is
            // about to trigger, a frame or so from now - doesn't immediately purge/cut this
            // confirmation off; see that flag's own comment for the full reasoning.
            if (pSapiVoice) {
                std::wstring toSpeak = L"Confirmed, launching " + program.displayName + L" now.";
                std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
                std::cout << "[Narrate:StartMenu] \"" << toSpeakNarrow << "\"" << std::endl;
                SpeakFfuiNarration(toSpeak.c_str());
            }
            suppressNextMenuCloseRenarration = true;

            // Don't wait on the launch itself - it continues on launchThread in the background
            // (see beginLaunch()'s own comment).
            state_ = State::Idle;
            beginLaunch(program);
            MenuSystem::getInstance().closeAll();
        };
        items.push_back(item);
    }

    MenuItemDef tryAgain;
    tryAgain.label = L"Try again";
    tryAgain.type = MenuItemType::Action;
    tryAgain.onSelect = [this]() {
        // Same ordering constraint as the result items' onSelect above - begin() (which reads
        // `this`) must run before closeAll() destroys this closure's own backing storage, not
        // after.
        begin();
        MenuSystem::getInstance().closeAll();
    };
    items.push_back(tryAgain);

    MenuItemDef cancel;
    cancel.label = L"Cancel";
    cancel.type = MenuItemType::Back;
    items.push_back(cancel);

    MenuSystem::getInstance().createMenu(L"Start programs", items, announcement);
    state_ = State::ShowingResults;
}

void StartMenuFlow::beginLaunch(const ProgramLauncher::InstalledProgram& program) {
    // "Whichever slot was used last" - read here, synchronously, on the haptic thread, before
    // handing off to launchThread below - see WindowManager::assignWindowToSlot()'s own comment.
    int targetSlot = WindowManager::getInstance().currentActiveSlotIndex;

    launchThread = std::jthread([program, targetSlot](std::stop_token stoken) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

        HWND launchedHwnd = NULL;
        bool launched = ProgramLauncher::launchAndDetectWindow(program, stoken, launchedHwnd);

        if (launched && launchedHwnd != NULL) {
            // Both safe to call from any thread - see assignWindowToSlot()'s and loadSlot()'s own
            // comments (windowMutex-guarded / plain HWND calls, matching how the scanner and
            // worker threads already call into WindowManager elsewhere in this codebase).
            // assignWindowToSlot() runs first so windowWallSlotsMap already has launchedHwnd in
            // targetSlot by the time loadSlot() re-derives it - no need to pass launchedHwnd
            // through explicitly (see loadSlot()'s own comment for why that's deliberate).
            //
            // "Is it possible to detect when the program has successfully started... and have
            // the narrator confirm when it is?" - launchAndDetectWindow() just did exactly that
            // detection (it can take a while - the request specifically calls out MS Office as
            // an example - which is exactly why this whole launch runs on its own background
            // thread rather than blocking the haptic thread). The "Now open: " prefix rides
            // along on loadSlot()'s own single "Slot N, appName" utterance rather than being a
            // separate Speak() call of its own - see loadSlot()'s own comment for why a second,
            // separately-timed announcement here would just race with (and likely get cut off
            // by, or itself cut off) that one.
            WindowManager::getInstance().assignWindowToSlot(launchedHwnd, targetSlot);
            WindowManager::getInstance().loadSlot(targetSlot, L"Now open: ");
        }
        else if (pSapiVoice) {
            std::cout << "[Narrate:StartMenu] \"Sorry, I couldn't open that program.\"" << std::endl;
            SpeakFfuiNarration(L"Sorry, I couldn't open that program.");
        }

        CoUninitialize();
    });
}
