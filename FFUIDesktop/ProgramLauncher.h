#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <stop_token>

// Enumerates, searches, and launches installed Start Menu programs for the "Start programs"
// haptic flow (see StartMenuFlow.h) - "search through installed programs (as in the windows
// start menu)... if a program is selected, launch it", per the request. Entirely worker-thread
// code (VoiceAssistant's own worker thread runs this - see StartMenuFlow.h's own comment for
// why a second thread wasn't spun up instead) - every method here blocks (file I/O, process
// launch, window-detection polling) and must never be called from the haptic thread.
class ProgramLauncher {
public:
    struct InstalledProgram {
        std::wstring displayName;   // the shortcut's filename, minus the .lnk extension
        std::wstring shortcutPath;  // full path to the .lnk itself
    };

    // Walks %ProgramData%\Microsoft\Windows\Start Menu\Programs (all users) and
    // %AppData%\Microsoft\Windows\Start Menu\Programs (current user) recursively, collecting
    // every .lnk shortcut found - the same source Windows' own Start Menu search indexes from.
    // Stateless/re-walks from scratch every call (no caching) - simplicity over performance,
    // matching this feature's "occasional, user-initiated, worker-thread-only" usage pattern; a
    // full walk of these two folders isn't expected to be large enough for that to matter.
    static std::vector<InstalledProgram> enumerate();

    // Simple case-insensitive scoring over enumerate()'s results (prefix match scores highest,
    // then substring match, then a loose all-words-present match, to tolerate small spacing/
    // punctuation differences a dictated multi-word query might pick up from whisper) - not
    // attempting to reproduce Explorer's own fuzzy ranking exactly, just usably close. Returns
    // at most maxResults, best matches first; empty if nothing scores above zero.
    static std::vector<InstalledProgram> search(const std::wstring& query, int maxResults = 3);

    // Launches program via ShellExecuteExW (SEE_MASK_NOCLOSEPROCESS, so a process handle/PID is
    // available as a secondary signal), then polls WindowScanner::fetchAllOpenWindows() every
    // ~200ms for a HWND that wasn't open in a snapshot taken immediately before launching -
    // preferring one whose owning process image name matches the shortcut's resolved target, if
    // more than one new window appears - until timeoutMs elapses or stoken is cancelled.
    // Best-effort by nature: a launcher-stub process, or a UWP/Store app not backed by a .lnk at
    // all, may not be reliably detected - acceptable for v1, matching this codebase's existing
    // "generous but bounded, not guaranteed" tone elsewhere (WhisperTranscriber's own transcribe
    // timeout, AssistantSetup's download waits). Returns true and sets outHwnd on success.
    //
    // Default raised from 15 to 30 seconds this round ("some e.g. MS office can have a rather
    // long delay [starting]") - this already ran on its own background thread even before this
    // change (see StartMenuFlow.h's own comment), so the haptic thread was never blocked by the
    // old, shorter timeout either; this just gives a genuinely slow-starting app (a cold Office
    // launch, or one triggering a Windows Installer repair/first-run step) more room to actually
    // be caught before this gives up and falls back to "sorry, I couldn't open that program".
    static bool launchAndDetectWindow(const InstalledProgram& program, std::stop_token stoken,
        HWND& outHwnd, DWORD timeoutMs = 30000);

private:
    // Resolves a .lnk's target executable path via IShellLinkW/IPersistFile (COM - the calling
    // thread must already be CoInitialize'd; VoiceAssistant's worker thread already is, at the
    // top of workerLoop()). Used only to name-match a candidate window's owning process in
    // launchAndDetectWindow() above, NOT to decide what to launch - the .lnk itself is what gets
    // executed, so its own embedded working directory/arguments/icon are honored exactly like a
    // real Start Menu click would. Returns an empty string on failure - launchAndDetectWindow()
    // simply falls back to "no name preference, take the first new window" in that case.
    static std::wstring resolveShortcutTarget(const std::wstring& shortcutPath);
};
