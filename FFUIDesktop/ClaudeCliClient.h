#pragma once

#include <windows.h>
#include <string>
#include <optional>

struct ClaudeCliResult {
	bool success = false;
	std::string replyText;   // "result" field - the text to speak to the user
	std::string sessionId;   // "session_id" field - fed back in as --resume for continuity
	std::string rawError;    // diagnostics only, never spoken aloud
};

//Shells out to the Claude Code CLI in non-interactive mode ("claude -p ...") to get a text
//reply from Claude, reusing whatever subscription login the user already set up with
//`claude` -> /login - no separate API key needed, as long as we never pass --bare (the one
//flag that switches Claude Code over to requiring ANTHROPIC_API_KEY and skips subscription
//login entirely).
//
//Deliberately locked down: this answers spoken, possibly-mis-transcribed questions from a
//blind user, so it must never let Claude execute a Bash command, edit a file, or call an MCP
//tool. --allowedTools "" plus a dedicated always-empty working directory (so no project-level
//.mcp.json/.claude/settings.json/CLAUDE.md is ever discovered) cover project-scoped risk, but
//NOT user-scoped config under %USERPROFILE%\.claude\ - that's a one-time manual audit on the
//target machine, not something this class can guarantee. See the project's voice-assistant
//plan for the full reasoning.
class ClaudeCliClient {
public:
	ClaudeCliClient();

	//Runs one claude -p exchange and blocks until it returns or times out (30s).
	//previousSessionId may be empty for a fresh conversation, or a session_id from a prior
	//successful ask() to continue that conversation via --resume.
	//
	//Call from the voice worker thread only, never the haptic thread - this spawns a process
	//and can block for several seconds.
	ClaudeCliResult ask(const std::string& promptUtf8, const std::string& previousSessionId);

	//Sets the resolved full path to claude.exe, discovered once by AssistantSetup at startup
	//(and re-set, to the same value, after a reactive re-login). ask() invokes this explicit
	//path rather than a bare "claude" - a bare command name would depend on this process's own
	//PATH, which won't reflect a PATH change AssistantSetup's installer just made until
	//FFUIDesktop itself restarts. Must be called at least once before the first ask(); ask()
	//fails fast with a clear error if it hasn't been.
	void setCliPath(const std::wstring& path) { cliPath = path; }

	//Small UTF-8/UTF-16 conversion helpers, public because VoiceAssistant needs them too (to
	//turn recognized speech into a UTF-8 prompt, and a UTF-8 reply back into a wide string for
	//ISpVoice::Speak).
	static std::wstring utf8ToWide(const std::string& utf8);
	static std::string wideToUtf8(const std::wstring& wide);

private:
	static constexpr DWORD PROCESS_TIMEOUT_MS = 30000;

	std::wstring cliPath;  //empty until setCliPath() is called - see its comment above

	std::wstring assistantWorkDir;  //resolved and created lazily on first use
	bool ensureWorkDirExists();

	std::wstring buildCommandLine(const std::string& promptUtf8, const std::string& previousSessionId) const;

	//Correctly quotes a single argument for CreateProcess's command line, following the
	//documented Windows argv-parsing rules (doubling backslashes before a literal quote,
	//etc.) - not the naive "just wrap it in quotes" approach, which breaks on an argument
	//that itself contains a backslash or quote.
	static std::wstring escapeForCommandLine(const std::wstring& raw);

	//A small hand-rolled extractor for one top-level string field in a flat, known JSON
	//shape (Claude Code's --output-format json output) - not a general JSON parser. Handles
	//standard escape sequences including \uXXXX, but not surrogate pairs (characters outside
	//the Basic Multilingual Plane) - a documented fast-follow if that ever shows up mangled
	//in a reply.
	static std::optional<std::string> extractJsonStringField(const std::string& json, const std::string& fieldName);
};
