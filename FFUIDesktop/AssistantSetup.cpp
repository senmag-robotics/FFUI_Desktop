#include "AssistantSetup.h"
#include "FFUIDesktop.h"   //for pAssistantVoice, the assistant's own dedicated SAPI voice
#include "ClaudeCliClient.h"  //just for the wideToUtf8() helper - see the console logging below
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <winhttp.h>
#include <shlobj.h>

//WinHttp* for downloadFileWithProgress() - see its comment in AssistantSetup.h for why the
//whisper.cpp downloads moved off PowerShell's Invoke-WebRequest. SHCreateDirectoryExW (shell32)
//for ensureWhisperReady()'s directory creation, replacing a PowerShell New-Item call.
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

void AssistantSetup::speak(const std::wstring& text) {
	if (pAssistantVoice) {
		// Mirrors every narrator request to the console, tagged by source - "mirror all the
		// narrator requests to the console please", per the request. Uses wideToUtf8() (already
		// pulled in for the download-progress logging below) rather than the naive narrow-cast
		// used elsewhere in this codebase's debug prints, since setup step text is more likely
		// than most narration to include non-ASCII characters.
		std::cout << "[Narrate:AssistantSetup] \"" << ClaudeCliClient::wideToUtf8(text) << "\"" << std::endl;
		pAssistantVoice->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);

		//Blocks this call (worker-thread-only, same as everything else in this class - never
		//called from the haptic thread) until the utterance has actually finished playing,
		//rather than returning the moment it was merely queued. Now mostly belt-and-suspenders
		//rather than strictly required for correctness - pAssistantVoice is its own independent
		//SAPI voice (see FFUIDesktop::initDesktop()), so FFUI's own narration on pSapiVoice
		//can't purge it any more regardless of timing - but it still keeps setup's own
		//back-to-back narrations (e.g. this line, followed shortly by the next step's own
		//speak() call) from purging each other mid-sentence, and keeps the worker thread from
		//racing ahead into the next blocking step (e.g. spawning installCli()) while still
		//talking. A few seconds of added latency per narration is an acceptable trade for that.
		pAssistantVoice->WaitUntilDone(INFINITE);
	}
}

bool AssistantSetup::resolveCliPath() {
	wchar_t pathBuf[MAX_PATH];
	DWORD len = SearchPathW(NULL, L"claude.exe", NULL, MAX_PATH, pathBuf, NULL);
	if (len > 0 && len < MAX_PATH) {
		cliPath = pathBuf;
		return true;
	}

	wchar_t* userProfile = nullptr;
	size_t envLen = 0;
	bool found = false;
	if (_wdupenv_s(&userProfile, &envLen, L"USERPROFILE") == 0 && userProfile) {
		//The native installer's fixed install location (code.claude.com/docs/en/setup) -
		//checked directly so a just-completed installCli() is picked up without needing this
		//process's own PATH to refresh, which it won't until FFUI itself restarts.
		std::wstring candidate = std::wstring(userProfile) + L"\\.local\\bin\\claude.exe";
		DWORD attrs = GetFileAttributesW(candidate.c_str());
		if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			cliPath = candidate;
			found = true;
		}
	}
	if (userProfile) free(userProfile);
	return found;
}

bool AssistantSetup::installCli(std::stop_token stoken) {
	std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
		L"\"irm https://claude.ai/install.ps1 | iex\"";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back(L'\0');

	//No admin required for the native installer per code.claude.com/docs/en/setup.
	BOOL created = CreateProcessW(NULL, mutableCmd.data(), NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	if (!created) return false;

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
	bool finished = false;
	while (!stoken.stop_requested() && std::chrono::steady_clock::now() < deadline) {
		if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
			finished = true;
			break;
		}
	}
	if (!finished) TerminateProcess(pi.hProcess, 1);

	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return finished && exitCode == 0;
}

bool AssistantSetup::isLikelyAuthenticated() const {
	wchar_t* userProfile = nullptr;
	size_t envLen = 0;
	bool exists = false;
	if (_wdupenv_s(&userProfile, &envLen, L"USERPROFILE") == 0 && userProfile) {
		//code.claude.com/docs/en/authentication: Windows credentials live at exactly this path
		//(unless CLAUDE_CONFIG_DIR is overridden - not supported here, a documented v1 gap).
		std::wstring credPath = std::wstring(userProfile) + L"\\.claude\\.credentials.json";
		DWORD attrs = GetFileAttributesW(credPath.c_str());
		exists = (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
	}
	if (userProfile) free(userProfile);
	return exists;
}

bool AssistantSetup::guidedInteractiveLogin(std::stop_token stoken) {
	if (cliPath.empty()) return false;  //shouldn't happen - resolveCliPath must succeed before this is called

	speak(L"Setting up your Claude account now. A window will open, and shortly after, your "
		L"web browser should open too. Please sign in with your Claude account there and "
		L"approve access. I'll let you know as soon as I detect it's done.");

	//Bare `claude`, no -p: this is the real interactive login flow, deliberately spawned
	//visible (no CREATE_NO_WINDOW) rather than hidden, since it's the one step that genuinely
	//needs an interactive console the user's own screen reader can drive if needed.
	std::wstring cmd = L"\"" + cliPath + L"\"";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back(L'\0');

	BOOL created = CreateProcessW(NULL, mutableCmd.data(), NULL, NULL, FALSE,
		0, NULL, NULL, &si, &pi);
	if (!created) {
		speak(L"I couldn't open the sign-in window. We can try again next time you start the program.");
		return false;
	}
	CloseHandle(pi.hThread);

	wchar_t* userProfile = nullptr;
	size_t envLen = 0;
	std::wstring credPath;
	if (_wdupenv_s(&userProfile, &envLen, L"USERPROFILE") == 0 && userProfile) {
		credPath = std::wstring(userProfile) + L"\\.claude\\.credentials.json";
	}
	if (userProfile) free(userProfile);

	bool loggedIn = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
	int ticksSinceCheck = 0;
	while (!stoken.stop_requested() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		if (++ticksSinceCheck >= 8) {  //check the filesystem roughly every 2s, not every 250ms
			ticksSinceCheck = 0;
			if (!credPath.empty()) {
				DWORD attrs = GetFileAttributesW(credPath.c_str());
				if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
					loggedIn = true;
					break;
				}
			}
		}
	}

	//Deliberately don't TerminateProcess the login window here, whether we're bailing out for
	//shutdown or just timed out - it may still be mid-login from the user's perspective, and
	//killing it out from under them would be actively unhelpful. A harmless leftover console
	//window (on success) or one left for the user to finish/close on their own is an accepted
	//minor rough edge - see the plan addendum.
	CloseHandle(pi.hProcess);

	if (stoken.stop_requested()) return false;

	if (loggedIn) {
		speak(L"You're signed in. The assistant is ready to use.");
	}
	else {
		speak(L"We can finish setting this up next time you start the program.");
	}
	return loggedIn;
}

bool AssistantSetup::downloadFileWithProgress(std::stop_token stoken, const std::wstring& url,
	const std::wstring& destPath, const std::wstring& itemLabel) {

	//Tagged and printed to the console the same way runExchange() logs recognized speech - see
	//that comment for why UTF-8-via-wideToUtf8() rather than std::wcout. This entire function
	//used to fail silently from the caller's point of view (every failure branch in
	//ensureWhisperReady() speaks the same generic sentence, so a user report of "it failed" gave
	//no way to tell which of five-plus possible failure points was actually hit) - these lines
	//exist specifically so a real failure leaves something to diagnose from afterward.
	const std::string labelUtf8 = ClaudeCliClient::wideToUtf8(itemLabel);
	std::cout << "[Assistant Setup] Downloading " << labelUtf8 << " from "
		<< ClaudeCliClient::wideToUtf8(url) << std::endl;

	URL_COMPONENTS urlComp{};
	urlComp.dwStructSize = sizeof(urlComp);
	wchar_t hostBuf[256]{};
	wchar_t pathBuf[2048]{};
	wchar_t extraBuf[2048]{};
	urlComp.lpszHostName = hostBuf;
	urlComp.dwHostNameLength = ARRAYSIZE(hostBuf);
	urlComp.lpszUrlPath = pathBuf;
	urlComp.dwUrlPathLength = ARRAYSIZE(pathBuf);
	urlComp.lpszExtraInfo = extraBuf;
	urlComp.dwExtraInfoLength = ARRAYSIZE(extraBuf);

	if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
		std::cout << "[Assistant Setup] " << labelUtf8 << ": WinHttpCrackUrl failed, error "
			<< GetLastError() << std::endl;
		return false;
	}
	if (urlComp.nScheme != INTERNET_SCHEME_HTTPS) {
		std::cout << "[Assistant Setup] " << labelUtf8 << ": URL is not https, refusing" << std::endl;
		return false;  //both call sites are https; not expected
	}

	HINTERNET hSession = WinHttpOpen(L"FFUIDesktop/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		std::cout << "[Assistant Setup] " << labelUtf8 << ": WinHttpOpen failed, error "
			<< GetLastError() << std::endl;
		return false;
	}

	//Resolve/connect/send bounded fairly tight (this is just opening the connection); receive
	//left at a more generous 30s per underlying read, not a cap on the whole transfer - the
	//decile-progress loop below has no overall deadline of its own, matching the "let a slow
	//but working download keep going" intent that motivated this rewrite in the first place.
	WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);

	HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
	if (!hConnect) {
		std::cout << "[Assistant Setup] " << labelUtf8 << ": WinHttpConnect failed, error "
			<< GetLastError() << std::endl;
		WinHttpCloseHandle(hSession);
		return false;
	}

	std::wstring objectName = std::wstring(pathBuf) + extraBuf;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", objectName.c_str(), NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		std::cout << "[Assistant Setup] " << labelUtf8 << ": WinHttpOpenRequest failed, error "
			<< GetLastError() << std::endl;
		WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
		return false;
	}

	//WinHttp follows same-scheme redirects (https->https) automatically by default - both
	//GitHub's "latest/download" link and Hugging Face's "resolve/main" link redirect to their
	//actual asset host, and neither downgrades to http, so no extra handling is needed here.
	BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (sent) sent = WinHttpReceiveResponse(hRequest, NULL);

	bool ok = sent != FALSE;
	if (!ok) {
		std::cout << "[Assistant Setup] " << labelUtf8 << ": send/receive request failed, error "
			<< GetLastError() << std::endl;
	}

	DWORD statusCode = 0;
	if (ok) {
		DWORD statusSize = sizeof(statusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
		ok = (statusCode == 200);
		if (!ok) {
			std::cout << "[Assistant Setup] " << labelUtf8 << ": unexpected HTTP status "
				<< statusCode << std::endl;
		}
	}

	DWORD contentLength = 0;
	if (ok) {
		DWORD clSize = sizeof(contentLength);
		//Not treated as fatal if missing - some CDNs omit it on a chunked response. Progress
		//narration and the completion check below are simply skipped in that case; the download
		//itself still proceeds and is trusted to be complete once the server closes the stream.
		if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &clSize, WINHTTP_NO_HEADER_INDEX)) {
			contentLength = 0;
		}
		std::cout << "[Assistant Setup] " << labelUtf8 << ": status 200, content-length "
			<< contentLength << std::endl;
	}

	if (ok) {
		std::ofstream outFile(destPath, std::ios::binary | std::ios::trunc);
		if (!outFile) {
			std::cout << "[Assistant Setup] " << labelUtf8 << ": couldn't open output file for writing" << std::endl;
			ok = false;
		}
		else {
			std::vector<char> buffer(65536);
			unsigned long long totalRead = 0;
			int lastDecile = 0;

			//Set only on a genuine transport-level failure below (WinHttpQueryDataAvailable or
			//WinHttpReadData returning FALSE) - kept separate from `ok` itself so the recovery
			//check just after the loop can tell "the transfer errored out" apart from "the
			//transfer ended normally but came up short", which need different handling.
			bool transportError = false;
			DWORD transportErrorCode = 0;

			for (;;) {
				if (stoken.stop_requested()) { ok = false; break; }

				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(hRequest, &available)) {
					transportError = true;
					transportErrorCode = GetLastError();
					ok = false;
					break;
				}
				if (available == 0) break;  //server signaled end of stream

				DWORD toRead = available < (DWORD)buffer.size() ? available : (DWORD)buffer.size();
				DWORD bytesRead = 0;
				if (!WinHttpReadData(hRequest, buffer.data(), toRead, &bytesRead)) {
					transportError = true;
					transportErrorCode = GetLastError();
					ok = false;
					break;
				}
				if (bytesRead == 0) break;

				outFile.write(buffer.data(), bytesRead);
				if (!outFile) { ok = false; break; }

				totalRead += bytesRead;
				if (contentLength > 0) {
					int decile = (int)((totalRead * 10) / contentLength);
					if (decile > 10) decile = 10;
					if (decile > lastDecile && decile < 10) {
						//Deliberately not narrating the 100% decile here - ensureWhisperReady()
						//already speaks its own completion line once every step has succeeded.
						lastDecile = decile;
						speak(itemLabel + L" download " + std::to_wstring(decile * 10) + L" percent done.");
					}
				}
			}
			outFile.close();

			//Two edge cases discovered from a real-world failure report ("it failed right as
			//the download finished"), both about the mismatch between "the loop ended" and "we
			//actually have the whole file":
			//
			//1. A transport error (WinHttpQueryDataAvailable/WinHttpReadData returning FALSE)
			//   hitting AFTER every promised byte has already been written to disk - e.g. a CDN
			//   resetting the connection right after the last chunk instead of closing it
			//   cleanly - used to discard a perfectly complete file. If we already have
			//   contentLength bytes on disk, the transfer itself succeeded; whatever the socket
			//   did immediately afterward doesn't matter.
			//2. The opposite bug: the loop's normal exit (the server reporting zero bytes
			//   available) was previously treated as success unconditionally, even if far fewer
			//   bytes than contentLength had actually arrived - silently writing a truncated
			//   file that would go on to fail (or behave oddly) much later, at whatever point
			//   something first tried to use it. Now checked and reported explicitly.
			if (contentLength > 0) {
				if (!ok && transportError && totalRead >= contentLength) {
					std::cout << "[Assistant Setup] " << labelUtf8
						<< ": transport error " << transportErrorCode
						<< " after all " << totalRead << " bytes were already received - treating as success" << std::endl;
					ok = true;
				}
				else if (ok && totalRead < contentLength) {
					std::cout << "[Assistant Setup] " << labelUtf8 << ": stream ended early, got "
						<< totalRead << " of " << contentLength << " bytes" << std::endl;
					ok = false;
				}
			}

			std::cout << "[Assistant Setup] " << labelUtf8 << ": finished, " << totalRead
				<< " bytes written, ok=" << (ok ? "true" : "false") << std::endl;
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if (stoken.stop_requested()) return false;
	return ok;
}

bool AssistantSetup::ensureWhisperReady(std::stop_token stoken) {
	wchar_t* localAppData = nullptr;
	size_t envLen = 0;
	std::wstring whisperDir;
	if (_wdupenv_s(&localAppData, &envLen, L"LOCALAPPDATA") == 0 && localAppData) {
		whisperDir = std::wstring(localAppData) + L"\\FFUIDesktop\\whisper";
	}
	if (localAppData) free(localAppData);
	if (whisperDir.empty()) return false;  //shouldn't happen - %LOCALAPPDATA% is always set

	//whisper-cli.exe is NOT at the root of the extracted archive - confirmed directly by
	//downloading and inspecting a real whisper-bin-x64.zip: every binary and DLL in the release
	//(whisper-cli.exe, ggml.dll, ggml-base.dll, whisper.dll, and several others whisper-cli.exe
	//needs alongside it to load at all) sits under a top-level "Release\" folder inside the zip,
	//so Expand-Archive -DestinationPath whisperDir produces whisperDir\Release\whisper-cli.exe,
	//not whisperDir\whisper-cli.exe. This - not antivirus interference, which a real report ruled
	//out - was the actual cause of "the program downloaded but wasn't usable afterward": the
	//previous path here simply pointed at a file that was never going to exist. whisper-cli.exe
	//is invoked by its full resolved path (see WhisperTranscriber), so leaving it inside Release\
	//rather than moving it out is fine - Windows' default DLL search order checks the executable's
	//own directory first, so its sibling DLLs are still found there.
	std::wstring exeCandidate = whisperDir + L"\\Release\\whisper-cli.exe";
	std::wstring modelCandidate = whisperDir + L"\\ggml-base.en.bin";

	//A literal single quote in any of these paths (only realistically possible if the Windows
	//username itself contains one, e.g. "O'Brien") would otherwise break out of the
	//single-quoted PowerShell string literal used below for Expand-Archive - PowerShell's own
	//escape for a literal ' inside a '...' string is doubling it to ''. Applied defensively;
	//%LOCALAPPDATA%-derived paths essentially never actually hit this, but it's a one-line
	//guard against a real, if rare, failure mode.
	auto escapeForPowerShellLiteral = [](std::wstring s) {
		std::wstring result;
		result.reserve(s.size());
		for (wchar_t ch : s) {
			if (ch == L'\'') result += L"''";
			else result += ch;
		}
		return result;
	};

	//Checks size, not just existence: GetFileAttributesEx also reports a file's size for free,
	//and a plain existence check can't tell a real file apart from a zero-byte one - which is
	//exactly what's left behind if antivirus quarantines/strips a freshly-downloaded,
	//unsigned .exe (a real possibility for whisper-cli.exe specifically, fresh out of a zip
	//from GitHub, tagged with the internet Mark-of-the-Web) sometime after this function itself
	//already saw it land on disk successfully. A quick per-file console log line means a report
	//of "it failed" can be told apart from "the download succeeded but something removed it
	//afterward" without needing to reproduce the problem again.
	auto fileSize = [](const std::wstring& path) -> long long {
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return -1;
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
		ULARGE_INTEGER size;
		size.HighPart = data.nFileSizeHigh;
		size.LowPart = data.nFileSizeLow;
		return (long long)size.QuadPart;
	};

	auto bothExist = [&]() {
		long long exeSize = fileSize(exeCandidate);
		long long modelSize = fileSize(modelCandidate);
		if (exeSize == 0 || modelSize == 0) {
			std::cout << "[Assistant Setup] whisper files present but zero-length (exe="
				<< exeSize << ", model=" << modelSize
				<< ") - likely removed by antivirus after download; check Windows Defender's "
				<< "protection history and consider excluding %LOCALAPPDATA%\\FFUIDesktop\\whisper"
				<< std::endl;
		}
		return exeSize > 0 && modelSize > 0;
	};

	//Cheap existence/size check only, same spirit as isLikelyAuthenticated() - if a previous run
	//(or a manual copy by the user) already left both files in place, there's nothing to download.
	if (bothExist()) {
		whisperExePath = exeCandidate;
		whisperModelPath = modelCandidate;
		speechState.store(SpeechSetupState::Ready);
		return true;
	}

	//Tracked on speechState, not `state` - this function is shared by run() (the full CLI+login+
	//speech sequence) and runSpeechOnly() (speech alone, with no CLI/login involved at all), and
	//`state` means "how far along is the FULL assistant setup", which runSpeechOnly() callers
	//never touch. speechState is the one atomic both paths agree on for "is whisper ready".
	speechState.store(SpeechSetupState::InstallingSpeech);
	speak(L"Setting up local speech recognition now. This downloads about one hundred fifty "
		L"megabytes and may take a minute or two, depending on your connection. I'll read out "
		L"the progress as it goes.");

	//Native directory creation rather than a PowerShell New-Item call - one less subprocess
	//round trip, and keeps this step from sharing any deadline with the network calls below.
	//Both ERROR_ALREADY_EXISTS and ERROR_FILE_EXISTS are success cases here (the directory, or
	//part of its path, already existing is fine).
	int createResult = SHCreateDirectoryExW(NULL, whisperDir.c_str(), NULL);
	if (createResult != ERROR_SUCCESS && createResult != ERROR_ALREADY_EXISTS && createResult != ERROR_FILE_EXISTS) {
		std::cout << "[Assistant Setup] SHCreateDirectoryExW failed, error " << createResult << std::endl;
		speak(L"I couldn't set up local speech recognition automatically - I wasn't able to "
			L"create the folder it needs. You may need to check your internet connection.");
		return false;
	}
	if (stoken.stop_requested()) return false;

	std::wstring zipPath = whisperDir + L"\\whisper-bin-x64.zip";

	//Both URLs are the project's own official distribution points, not a third-party mirror:
	//the "releases/latest/download/" form always resolves to whichever whisper.cpp release is
	//newest without needing to track version numbers here, and the model comes from its
	//official Hugging Face home under the whisper.cpp maintainer's own account. No elevation
	//needed for any of this - everything lands under the user's own %LOCALAPPDATA%, unlike the
	//DISM-based English-speech-pack path this replaced, which needed a UAC prompt.
	//
	//Each of the three steps below (zip download, extraction, model download) is independently
	//bounded now, rather than sharing the single 5-minute deadline the old all-in-one
	//PowerShell script used - that shared deadline, combined with Invoke-WebRequest's default
	//progress-bar rendering (documented to slow large downloads down severely unless
	//$ProgressPreference is set to SilentlyContinue), is the suspected cause of setup being
	//reported as failed even after the download had seemingly completed: the process was most
	//likely still being killed mid-transfer by TerminateProcess once the shared deadline expired.
	if (!downloadFileWithProgress(stoken,
			L"https://github.com/ggml-org/whisper.cpp/releases/latest/download/whisper-bin-x64.zip",
			zipPath, L"The speech program")) {
		DeleteFileW(zipPath.c_str());
		if (!stoken.stop_requested()) {
			speak(L"I couldn't download the speech recognition program automatically. "
				L"You may need to check your internet connection.");
		}
		return false;
	}
	if (stoken.stop_requested()) { DeleteFileW(zipPath.c_str()); return false; }

	//Extraction is local disk I/O only, not subject to the network-progress-bar slowdown that
	//motivated moving the downloads off PowerShell above, so it stays a small, separately-timed
	//Expand-Archive call - $ProgressPreference is set here too, defensively, since
	//Expand-Archive has its own default progress bar that's unnecessary overhead in a hidden,
	//non-interactive console.
	std::wstring safeWhisperDir = escapeForPowerShellLiteral(whisperDir);
	std::wstring safeZipPath = escapeForPowerShellLiteral(zipPath);
	std::wstring psCommand =
		L"$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; "
		L"Expand-Archive -Path '" + safeZipPath + L"' -DestinationPath '" + safeWhisperDir + L"' -Force";
	std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + psCommand + L"\"";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back(L'\0');

	BOOL created = CreateProcessW(NULL, mutableCmd.data(), NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	bool extractOk = false;
	if (created) {
		//Local disk extraction of a ~75MB archive - one minute is generous.
		const auto extractDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
		bool finished = false;
		while (!stoken.stop_requested() && std::chrono::steady_clock::now() < extractDeadline) {
			if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
				finished = true;
				break;
			}
		}
		if (!finished) TerminateProcess(pi.hProcess, 1);

		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		extractOk = finished && exitCode == 0;
		if (!extractOk) {
			std::cout << "[Assistant Setup] Expand-Archive failed - created=" << (created ? "true" : "false")
				<< ", finished=" << (finished ? "true" : "false") << ", exitCode=" << exitCode << std::endl;
		}
	}
	else {
		std::cout << "[Assistant Setup] Couldn't launch PowerShell for Expand-Archive, error "
			<< GetLastError() << std::endl;
	}

	//The zip is only scratch space - only whisper-cli.exe (plus whatever DLLs shipped
	//alongside it in the same archive) and the model file need to stick around.
	DeleteFileW(zipPath.c_str());

	if (stoken.stop_requested()) return false;
	if (!extractOk) {
		speak(L"I couldn't extract the speech recognition program automatically. "
			L"You may need to check your internet connection.");
		return false;
	}

	if (!downloadFileWithProgress(stoken,
			L"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin",
			modelCandidate, L"The speech model")) {
		if (!stoken.stop_requested()) {
			speak(L"I couldn't download the speech recognition model automatically. "
				L"You may need to check your internet connection.");
		}
		return false;
	}
	if (stoken.stop_requested()) return false;

	if (bothExist()) {
		whisperExePath = exeCandidate;
		whisperModelPath = modelCandidate;
		speechState.store(SpeechSetupState::Ready);
		speak(L"Local speech recognition is ready.");
		return true;
	}

	//Both downloads and the extraction reported success just above, yet the files aren't
	//usable now - bothExist() already logged the exact sizes it found. In practice this points
	//at something removing or truncating the files after they landed (most likely antivirus
	//quarantining the freshly-extracted, unsigned whisper-cli.exe), not at the network - so this
	//is deliberately a different spoken message from the download/extraction failures above.
	speak(L"The speech program downloaded, but one of its files isn't usable afterward - this "
		L"can happen if antivirus software removes it. You may want to check your antivirus "
		L"software's history.");
	return false;
}

void AssistantSetup::run(std::stop_token stoken) {
	state.store(SetupState::CheckingCli);
	if (!resolveCliPath()) {
		state.store(SetupState::InstallingCli);
		speak(L"Setting up the Claude assistant for the first time. This may take a minute.");
		if (stoken.stop_requested()) { state.store(SetupState::Failed); return; }

		if (!installCli(stoken) || !resolveCliPath()) {
			state.store(SetupState::Failed);
			if (!stoken.stop_requested()) {
				speak(L"I couldn't install the Claude assistant automatically. "
					L"You may need to check your internet connection.");
			}
			return;
		}
	}
	if (stoken.stop_requested()) { state.store(SetupState::Failed); return; }

	state.store(SetupState::CheckingAuth);
	if (!isLikelyAuthenticated()) {
		state.store(SetupState::AwaitingLogin);
		if (!guidedInteractiveLogin(stoken)) {
			state.store(SetupState::Failed);
			return;
		}
	}
	if (stoken.stop_requested()) { state.store(SetupState::Failed); return; }

	state.store(SetupState::CheckingSpeech);
	if (!ensureWhisperReady(stoken)) {
		state.store(SetupState::Failed);
		//ensureWhisperReady() only ever moves speechState forward (InstallingSpeech/Ready) on its
		//own - it has no single common return point to hang a Failed write off internally, so
		//both callers set it here on a false return instead. Skipped on a plain stop_token
		//cancellation the same way `state` above isn't specially distinguished from a real
		//failure either - nothing reads either atomic again once the process is shutting down.
		speechState.store(SpeechSetupState::Failed);
		return;
	}

	state.store(SetupState::Ready);
	//speechState was already left at Ready by ensureWhisperReady() itself just above - nothing
	//further to do here.
}

void AssistantSetup::runSpeechOnly(std::stop_token stoken) {
	//No CLI resolution, no login - "it should be possible to have the speech-text for Dictate
	//without having the claude AI plugin installed / enabled", per the request. This is the
	//entire speech-only setup sequence: just whisper.cpp. ensureWhisperReady() itself keeps
	//speechState current on every path through it (InstallingSpeech while downloading, Ready on
	//success); this wrapper only needs to record a failure, mirroring how run() does the same for
	//the full sequence just above.
	if (!ensureWhisperReady(stoken)) {
		speechState.store(SpeechSetupState::Failed);
	}
}

void AssistantSetup::redoLogin(std::stop_token stoken) {
	state.store(SetupState::AwaitingLogin);
	if (guidedInteractiveLogin(stoken)) {
		state.store(SetupState::Ready);
	}
	else {
		state.store(SetupState::Failed);
	}
}
