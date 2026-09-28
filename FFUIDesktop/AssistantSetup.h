#pragma once

#include <windows.h>
#include <string>
#include <atomic>
#include <stop_token>

//Where AssistantSetup::run() has gotten to. VoiceAssistant reads this (a plain atomic load,
//safe from the haptic thread too) to decide whether a tap-then-hold gesture should actually
//start listening or just play a "not ready yet" cue.
enum class SetupState {
	NotStarted,
	CheckingCli,
	InstallingCli,
	CheckingAuth,
	AwaitingLogin,
	CheckingSpeech,
	InstallingSpeech,
	Ready,
	Failed
};

//Whether whisper.cpp alone (no Claude CLI, no login) is provisioned - tracked completely
//separately from SetupState above. Gareth's own read of the codebase was right: dictation and
//the Claude AI assistant were only coupled by run()'s ordering (whisper provisioned last, behind
//CLI+login succeeding first), not by any real technical dependency - whisper.cpp itself doesn't
//need the Claude CLI or an account at all. This atomic is the source of truth for "is dictation
//usable" independent of whether the full assistant has ever been set up; runSpeechOnly() below
//is the speech-only entry point that updates it, and ensureWhisperReady() (shared by both paths)
//is what actually keeps it current regardless of which one called it.
enum class SpeechSetupState {
	NotStarted,
	InstallingSpeech,
	Ready,
	Failed
};

//Makes the voice assistant self-configuring: finds or silently installs the Claude Code CLI,
//walks the user through the one unavoidable manual step (a real browser OAuth login - there is
//no supported non-interactive alternative; this is a deliberate security boundary on
//Anthropic's side, not a gap to engineer around), and finds or downloads whisper.cpp (the local
//speech-to-text engine - see WhisperTranscriber.h for why this replaced SAPI's built-in
//recognizer). See the project's voice-assistant plan addendum for the research this design is
//based on (Claude Code install/auth mechanics confirmed against code.claude.com's docs in
//August 2026; the switch to whisper.cpp came later, after real-world testing showed SAPI's
//recognition accuracy too weak to rely on - the CheckingSpeech/InstallingSpeech states below
//used to mean "check/provision a Windows SAPI language pack via DISM", now mean "check/download
//whisper-cli.exe and its model file" instead, which needs no elevation at all, unlike the DISM
//path it replaced).
//
//Owned by VoiceAssistant and driven entirely from its worker thread: run() is called once, at
//the top of workerLoop(), before the gesture-triggered listen loop starts, so none of this ever
//blocks the haptic thread. Everything here can block for a long time (installer/model
//downloads, and especially the guided login, which waits on the user) - every wait loop is
//sliced and stop_token-aware so app shutdown isn't held hostage by an incomplete setup.
class AssistantSetup {
public:
	//Runs the full sequence: resolve/install the CLI, check/guide login, check/download
	//whisper.cpp. Blocks (possibly for minutes, if it has to wait on a guided login or a slow
	//download) until it reaches Ready or Failed, or stoken is cancelled.
	//
	//Opt-in, not automatic: VoiceAssistant only calls this in response to an explicit user
	//confirmation (the two-detent prompt - see SetupConfirmationPrompt), never eagerly. Safe to
	//call more than once across a process's lifetime as a retry after a prior call left state at
	//Failed (each step re-checks real-world state before acting, so re-running is idempotent) -
	//just never call it again while a previous call is still in flight.
	void run(std::stop_token stoken);

	//Re-runs just the login step. Called reactively by VoiceAssistant::runExchange() when a
	//live ask() fails with what looks like an expired/missing login, rather than the full
	//sequence - the CLI install and speech provisioning already succeeded once and don't need
	//redoing. Leaves state at Ready on success, Failed otherwise.
	void redoLogin(std::stop_token stoken);

	//Provisions whisper.cpp alone - no Claude CLI resolution/install, no login. Blocks (possibly
	//for a minute or two, if it has to download) until speechState reaches Ready or Failed, or
	//stoken is cancelled. Opt-in the same way run() is - VoiceAssistant only calls this in
	//response to the dictation gesture's own confirmation prompt, never eagerly - and safe to
	//call more than once (ensureWhisperReady()'s own existence check makes re-running a no-op
	//once the files are already there, including after a prior run() already provisioned them).
	void runSpeechOnly(std::stop_token stoken);

	std::atomic<SetupState> state{ SetupState::NotStarted };

	//See SpeechSetupState's own comment above for why this exists separately from `state`.
	//Reflects reality regardless of which entry point (run() or runSpeechOnly()) provisioned
	//whisper - both funnel through the same ensureWhisperReady(), which is what actually keeps
	//this current.
	std::atomic<SpeechSetupState> speechState{ SpeechSetupState::NotStarted };

	//Full path to the resolved claude.exe once resolveCliPath() or installCli() has succeeded;
	//empty otherwise. ClaudeCliClient invokes this explicit path rather than a bare "claude" -
	//see resolveCliPath()'s comment for why that matters.
	std::wstring resolvedCliPath() const { return cliPath; }

	//Full paths to whisper-cli.exe and its model file once ensureWhisperReady() has succeeded;
	//empty otherwise. WhisperTranscriber invokes these explicit paths - see
	//WhisperTranscriber::setPaths().
	std::wstring resolvedWhisperExePath() const { return whisperExePath; }
	std::wstring resolvedWhisperModelPath() const { return whisperModelPath; }

private:
	std::wstring cliPath;
	std::wstring whisperExePath;
	std::wstring whisperModelPath;

	//Two-tier resolution: SearchPathW first (covers any pre-existing WinGet/npm/Homebrew/
	//native install already on this process's PATH), then the fixed native-installer path
	//directly (covers "installCli() just installed it in this same process's lifetime" -
	//FFUIDesktop's own inherited PATH won't reflect a PATH change an installer makes for new
	//processes until FFUI itself restarts, so PATH lookup alone can't be trusted right after
	//installing). Returns true and sets cliPath on success.
	bool resolveCliPath();

	//Runs the documented native-installer one-liner via PowerShell (no admin required per
	//code.claude.com's docs), waits for it to finish in stop_token-aware slices bounded to a
	//few minutes, then the caller retries resolveCliPath().
	bool installCli(std::stop_token stoken);

	//Cheap existence check only (no subprocess spawn) - see the plan addendum for why a live
	//probe on every startup isn't worth the added latency, and why real expiry is instead
	//caught reactively by VoiceAssistant::runExchange() and routed back through redoLogin().
	bool isLikelyAuthenticated() const;

	//The one unavoidable manual step. Spawns `claude` as its own visible console process
	//(deliberately not hidden - this is the real interactive OAuth flow, and the user's own
	//screen reader can drive the browser handoff if needed), narrates what's happening via
	//pAssistantVoice, and polls for the credentials file to appear. Bounded; returns false on
	//timeout or cancellation rather than waiting forever.
	bool guidedInteractiveLogin(std::stop_token stoken);

	//Checks whether whisper-cli.exe and its model file already exist under
	//%LOCALAPPDATA%\FFUIDesktop\whisper\ (a plain file-existence check, no subprocess) and, if
	//not, downloads both: the CLI binary (as a zip, then expanded in place) from the official
	//ggml-org/whisper.cpp GitHub releases, and the model file from its official Hugging Face
	//home - both the project's own canonical distribution points, not a third-party mirror.
	//Needs no elevation (everything lands under the user's own %LOCALAPPDATA%), unlike the old
	//DISM-based English-speech-pack path this replaced. Sets whisperExePath/whisperModelPath on
	//success. Shared by both run() (as part of the full CLI+login+speech sequence) and
	//runSpeechOnly() (speech alone) - keeps speechState current itself, on every path through the
	//function, regardless of which of the two callers invoked it; callers only manage their own
	//SetupState/nothing further.
	bool ensureWhisperReady(std::stop_token stoken);

	//Downloads a single file over HTTPS via raw WinHTTP calls, writing straight to destPath, and
	//speaks a spoken progress update via speak() every time cumulative progress crosses another
	//10% of the total (from the response's Content-Length) - itemLabel names what's downloading
	//in that narration, e.g. "the whisper program" or "the speech model".
	//
	//Deliberately NOT implemented as a PowerShell Invoke-WebRequest call (which is what
	//ensureWhisperReady() used to use): Invoke-WebRequest renders a progress bar by default that
	//is documented to slow large downloads down severely (sometimes 10x or worse) unless
	//$ProgressPreference is set to SilentlyContinue - and even with that fix, PowerShell still
	//gives no way to observe byte-level progress from the outside for the spoken-progress
	//feature below. Raw WinHTTP sidesteps both problems in one piece of work: no progress-bar
	//overhead, and a normal read loop that can report on itself as it goes.
	//
	//Blocks the calling thread (worker-thread-only, same as the rest of this class) until the
	//download finishes, fails, or stoken is cancelled. Returns false - and leaves any partial
	//file on disk for the caller to clean up - on any failure (DNS/connect failure, non-200
	//response, a write error, or cancellation).
	bool downloadFileWithProgress(std::stop_token stoken, const std::wstring& url,
		const std::wstring& destPath, const std::wstring& itemLabel);

	static void speak(const std::wstring& text);
};
