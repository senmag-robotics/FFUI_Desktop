#include "ClaudeCliClient.h"
#include <vector>
#include <cstdlib>

ClaudeCliClient::ClaudeCliClient() {}

bool ClaudeCliClient::ensureWorkDirExists() {
	if (!assistantWorkDir.empty()) {
		return true;  //already resolved and created earlier in this process's lifetime
	}

	wchar_t* localAppData = nullptr;
	size_t len = 0;
	errno_t err = _wdupenv_s(&localAppData, &len, L"LOCALAPPDATA");
	if (err != 0 || !localAppData) {
		if (localAppData) free(localAppData);
		return false;
	}

	std::wstring baseDir = localAppData;
	free(localAppData);

	std::wstring appDir = baseDir + L"\\FFUIDesktop";
	std::wstring workDir = appDir + L"\\assistant_workdir";

	//CreateDirectoryW on a directory that already exists returns FALSE with
	//ERROR_ALREADY_EXISTS - that's success for our purposes, anything else is a real failure.
	if (!CreateDirectoryW(appDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		return false;
	}
	if (!CreateDirectoryW(workDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		return false;
	}

	assistantWorkDir = workDir;
	return true;
}

std::wstring ClaudeCliClient::utf8ToWide(const std::string& utf8) {
	if (utf8.empty()) return L"";
	int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), NULL, 0);
	if (sizeNeeded <= 0) return L"";
	std::wstring wide(sizeNeeded, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), sizeNeeded);
	return wide;
}

std::string ClaudeCliClient::wideToUtf8(const std::wstring& wide) {
	if (wide.empty()) return "";
	int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded <= 0) return "";
	std::string utf8(sizeNeeded, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), sizeNeeded, NULL, NULL);
	return utf8;
}

//Follows the documented Windows command-line quoting rules exactly (see Microsoft's "Everyone
//quotes command line arguments the wrong way"): backslashes are only special immediately
//before a double quote, so a naive "wrap it in quotes" approach silently corrupts any argument
//containing a literal backslash or quote character - both of which are plausible in speech-
//recognized text or in a Claude reply fed back in via --resume.
std::wstring ClaudeCliClient::escapeForCommandLine(const std::wstring& raw) {
	std::wstring result;
	result.push_back(L'"');

	for (auto it = raw.begin(); ; ++it) {
		unsigned numBackslashes = 0;
		while (it != raw.end() && *it == L'\\') {
			++it;
			++numBackslashes;
		}

		if (it == raw.end()) {
			//Escape all backslashes, but let the terminating quote we add below act as a
			//real metacharacter rather than being escaped itself.
			result.append(numBackslashes * 2, L'\\');
			break;
		}
		else if (*it == L'"') {
			result.append(numBackslashes * 2 + 1, L'\\');
			result.push_back(*it);
		}
		else {
			result.append(numBackslashes, L'\\');
			result.push_back(*it);
		}
	}

	result.push_back(L'"');
	return result;
}

std::wstring ClaudeCliClient::buildCommandLine(const std::string& promptUtf8, const std::string& previousSessionId) const {
	//Invoke the resolved full path (escaped/quoted the same as every other argument) rather
	//than a bare "claude" - see the header comment on setCliPath() for why that matters.
	std::wstring cmd = escapeForCommandLine(cliPath);

	auto appendArg = [&cmd](const std::wstring& arg) {
		cmd.push_back(L' ');
		cmd.append(escapeForCommandLine(arg));
		};

	appendArg(L"-p");
	appendArg(utf8ToWide(promptUtf8));
	appendArg(L"--output-format");
	appendArg(L"json");

	//Deliberately empty: no tool use is permitted for a voice-transcribed question from a
	//blind user - see the class comment in ClaudeCliClient.h for why.
	appendArg(L"--allowedTools");
	appendArg(L"");

	appendArg(L"--append-system-prompt");
	appendArg(L"You are a spoken voice assistant for a blind user of an accessibility haptic device. "
		L"Reply conversationally in one or two short sentences. Never use markdown, code blocks, "
		L"bullet points, or special formatting - your entire reply is read aloud verbatim by a "
		L"text-to-speech engine.");

	//No --bare: bare mode is faster to start but requires ANTHROPIC_API_KEY and skips the
	//subscription login entirely, which defeats the point of using the CLI here at all.

	if (!previousSessionId.empty()) {
		appendArg(L"--resume");
		appendArg(utf8ToWide(previousSessionId));
	}

	return cmd;
}

std::optional<std::string> ClaudeCliClient::extractJsonStringField(const std::string& json, const std::string& fieldName) {
	std::string key = "\"" + fieldName + "\"";
	size_t keyPos = json.find(key);
	if (keyPos == std::string::npos) return std::nullopt;

	size_t pos = keyPos + key.size();
	auto isJsonSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

	while (pos < json.size() && isJsonSpace(json[pos])) pos++;
	if (pos >= json.size() || json[pos] != ':') return std::nullopt;
	pos++;
	while (pos < json.size() && isJsonSpace(json[pos])) pos++;

	if (pos >= json.size() || json[pos] != '"') return std::nullopt;  //only handles string-valued fields
	pos++;  //past the opening quote

	std::string value;
	while (pos < json.size() && json[pos] != '"') {
		char c = json[pos];

		if (c != '\\' || pos + 1 >= json.size()) {
			value.push_back(c);
			pos++;
			continue;
		}

		char next = json[pos + 1];
		switch (next) {
		case '"':  value.push_back('"');  pos += 2; break;
		case '\\': value.push_back('\\'); pos += 2; break;
		case '/':  value.push_back('/');  pos += 2; break;
		case 'b':  value.push_back('\b'); pos += 2; break;
		case 'f':  value.push_back('\f'); pos += 2; break;
		case 'n':  value.push_back('\n'); pos += 2; break;
		case 'r':  value.push_back('\r'); pos += 2; break;
		case 't':  value.push_back('\t'); pos += 2; break;
		case 'u':
			if (pos + 5 < json.size()) {
				//4 hex digits -> one UTF-16 code unit -> re-encoded as UTF-8. Surrogate pairs
				//(characters outside the Basic Multilingual Plane) aren't reassembled - see
				//the header comment.
				std::string hex = json.substr(pos + 2, 4);
				unsigned int codepoint = 0xFFFD;
				try {
					codepoint = std::stoul(hex, nullptr, 16);
				}
				catch (...) {
					codepoint = 0xFFFD;  //replacement character on a malformed escape
				}

				if (codepoint <= 0x7F) {
					value.push_back(static_cast<char>(codepoint));
				}
				else if (codepoint <= 0x7FF) {
					value.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
					value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
				}
				else {
					value.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
					value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
					value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
				}
				pos += 6;
			}
			else {
				pos += 2;  //malformed \u escape right at the end of the string - just skip it
			}
			break;
		default:
			value.push_back(next);
			pos += 2;
			break;
		}
	}

	return value;
}

ClaudeCliResult ClaudeCliClient::ask(const std::string& promptUtf8, const std::string& previousSessionId) {
	ClaudeCliResult result;

	if (cliPath.empty()) {
		result.rawError = "Claude CLI path not resolved yet - AssistantSetup must complete before ask() is called";
		return result;
	}

	if (!ensureWorkDirExists()) {
		result.rawError = "could not create the assistant's dedicated working directory";
		return result;
	}

	std::wstring commandLine = buildCommandLine(promptUtf8, previousSessionId);

	SECURITY_ATTRIBUTES saAttr{};
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	HANDLE hStdOutRead = NULL, hStdOutWrite = NULL;
	if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
		result.rawError = "CreatePipe failed";
		return result;
	}
	//Only the write end should be inherited by the child - if the read end were inherited
	//too, the pipe would never see EOF (our own open read handle would keep it alive) even
	//after the child exits, and our ReadFile loop below would hang forever.
	SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

	//stdin and stderr both point at NUL: the prompt goes in via the command line, not stdin,
	//so the child should never wait on it; and keeping stdout single-purpose (just the JSON)
	//keeps the extractor above simple, rather than needing to separate interleaved stderr text.
	HANDLE hNul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		&saAttr, OPEN_EXISTING, 0, NULL);

	STARTUPINFOW si{};
	si.cb = sizeof(STARTUPINFOW);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hStdOutWrite;
	si.hStdError = hNul;
	si.hStdInput = hNul;

	PROCESS_INFORMATION pi{};

	//CreateProcessW can write into the buffer it's given, so this must be a mutable,
	//non-const wchar_t buffer rather than commandLine.c_str() directly.
	std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
	mutableCommandLine.push_back(L'\0');

	BOOL created = CreateProcessW(
		NULL,                      //application name - taken from the command line's first token instead
		mutableCommandLine.data(),
		NULL, NULL,
		TRUE,                      //inherit handles - required for the pipe/NUL redirection above
		CREATE_NO_WINDOW,          //this app is a Console-subsystem exe; without this a child console window flashes
		NULL,                      //inherit our environment, so the existing Claude Code login under
								   //%USERPROFILE% is found the same way it would be from an interactive shell
		assistantWorkDir.c_str(),
		&si,
		&pi);

	//Close our copies of the handles the child now owns - we don't need the write end or the
	//NUL handle in the parent process once the child has started.
	CloseHandle(hStdOutWrite);
	if (hNul) CloseHandle(hNul);

	if (!created) {
		CloseHandle(hStdOutRead);
		result.rawError = "CreateProcess failed, GetLastError=" + std::to_string(GetLastError());
		return result;
	}

	//Drain the pipe continuously rather than waiting for the process to exit first - if the
	//child writes more than the pipe's internal buffer can hold before anyone reads it, it
	//would block on its own write with nothing draining the other end, deadlocking both sides.
	std::string stdoutCapture;
	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(hStdOutRead, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
		stdoutCapture.append(buffer, bytesRead);
	}
	CloseHandle(hStdOutRead);

	DWORD waitResult = WaitForSingleObject(pi.hProcess, PROCESS_TIMEOUT_MS);
	if (waitResult == WAIT_TIMEOUT) {
		TerminateProcess(pi.hProcess, 1);
		result.rawError = "claude -p timed out after " + std::to_string(PROCESS_TIMEOUT_MS) + "ms";
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	if (!result.rawError.empty()) return result;  //timeout path above already set an error

	auto replyField = extractJsonStringField(stdoutCapture, "result");
	auto sessionField = extractJsonStringField(stdoutCapture, "session_id");

	if (!replyField.has_value() || replyField->empty()) {
		result.rawError = "no non-empty 'result' field in claude -p output: " + stdoutCapture.substr(0, 500);
		return result;
	}

	result.success = true;
	result.replyText = *replyField;
	if (sessionField.has_value()) result.sessionId = *sessionField;

	return result;
}
