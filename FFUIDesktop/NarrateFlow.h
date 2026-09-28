#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>

class FFUIObject;

//Drives the "Narrate" feature (a standalone AUX-button tap - see NARRATE_SPEC.md for the full
//design). Originally triggered by the rear button; moved onto AUX per "tap to narrate, hold to
//dictate, tap+hold for the AI assistant... keep the back button to right click" - see
//VoiceAssistant::tryConsumeTapNarrateEdge() for how a plain tap is distinguished from AUX's other
//two gestures.
//This is Stage 1 only: tap AUX (on its own) to hear whatever the stylus is currently on, through the new
//dedicated pNarrateVoice - either the current line of real text at the OS cursor's position (an
//email, a Word document, any other editable/selectable text - see readCurrent()'s own comment for
//why this is checked first and independently of everything else), or "Name, Role[, State]" for a
//button/list item/menu item/tile/window. A later stage, not attempted here, is called out in
//NARRATE_SPEC.md and this class's own comments where relevant: reaching into third-party content
//beyond the currently-focused window's own scanned elements via a full UI Automation tree walk,
//and the hold-Narrate navigation mode (scroll to move next/previous, select to descend, etc.)
//with its own auto-announce for state changes.
//
//A Meyer's singleton, matching WindowManager/MenuSystem/StartMenuFlow's own pattern. Entirely
//haptic-thread-owned - readCurrent() is only ever called from FFUIDesktop::updateFrame(), same as
//StartMenuFlow::update()/MenuSystem's own entry points, so no locking of its own state is needed
//(the one lock it does take, objectsListMutex, is the caller's own FFUIDesktop member, passed in
//by reference exactly the way StartMenuFlow::update() takes voiceAssistant by reference rather
//than reaching for a singleton - see that method's own comment).
class NarrateFlow {
private:
    NarrateFlow() {}
public:
    static NarrateFlow& getInstance() {
        static NarrateFlow instance;
        return instance;
    }
    NarrateFlow(NarrateFlow const&) = delete;
    void operator=(NarrateFlow const&) = delete;

    //Called when VoiceAssistant resolves a plain, standalone AUX tap (see
    //tryConsumeTapNarrateEdge()'s own comment). See FFUIDesktop::updateFrame()'s own gating - not
    //while the tutorial is active, mid-grab, mid-menu, or mid-Start-Menu-dictation, none of which
    //have a well-defined single "current object" the way ordinary browsing does. Looks for, in
    //order: the current line of real text at the OS cursor's position (an email, a Word document,
    //any other editable/selectable text content - see the .cpp's tryReadTextLineAtCursor() for
    //why this has to be its own independent UIA Text-pattern lookup rather than anything the scan
    //below can answer), then a third-party button/list item/menu item from the currently-focused
    //window (a ButtonObject with stylusIsOnThis() true - see ButtonObject.h; these are otherwise
    //entirely silent today, felt but never named, which was the original gap this stage closed),
    //then an FFUI Settings tile (GridTileObject), then a Program Tray entry or the active slot's
    //own window (WindowWallObject) - the first of these whose own stylusIsOnThis() equivalent is
    //true this frame is spoken. Speaks "Nothing here right now." if none of them currently has
    //the stylus on it, rather than staying silent - silence on a press reads as "did that even
    //register?" per this feature's own screen-reader-conventions research.
    //
    //objects/objectsListMutex are FFUIDesktop's own layers[0].objects and its guarding mutex,
    //passed by reference for the same reason StartMenuFlow::update() takes voiceAssistant by
    //reference - see this class's own header comment.
    void readCurrent(std::mutex& objectsListMutex, const std::vector<std::unique_ptr<FFUIObject>>& objects);
};
