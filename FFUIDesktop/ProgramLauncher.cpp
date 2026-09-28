#include "ProgramLauncher.h"
#include "WIndowScanner.h"
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <cwctype>
#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "shell32.lib")

namespace {
	// Recursively collects every .lnk under rootDir into out - shared by both Start Menu
	// Programs folders enumerate() walks. Silently skips a folder that doesn't exist (e.g. no
	// per-user Start Menu customizations yet) or that a permission error makes inaccessible,
	// rather than treating either as a hard failure - matches this feature's "best effort, don't
	// block the flow over one folder" spirit.
	void collectShortcuts(const std::wstring& rootDir, std::vector<ProgramLauncher::InstalledProgram>& out) {
		std::error_code ec;
		if (!std::filesystem::exists(rootDir, ec) || ec) return;

		std::filesystem::recursive_directory_iterator it(
			rootDir, std::filesystem::directory_options::skip_permission_denied, ec);
		std::filesystem::recursive_directory_iterator end;

		for (; !ec && it != end; it.increment(ec)) {
			const std::filesystem::directory_entry& entry = *it;

			std::error_code fileEc;
			if (!entry.is_regular_file(fileEc) || fileEc) continue;

			std::wstring ext = entry.path().extension().wstring();
			for (wchar_t& c : ext) c = (wchar_t)towlower(c);
			if (ext != L".lnk") continue;

			ProgramLauncher::InstalledProgram program;
			program.shortcutPath = entry.path().wstring();
			program.displayName = entry.path().stem().wstring();
			out.push_back(program);
		}
	}

	// Prefix match scores highest, then substring, then a loose "every word of the query shows
	// up somewhere in the name" match - see ProgramLauncher::search()'s own header comment for
	// why. Both strings are expected already-lowercased by the caller.
	int scoreMatch(const std::wstring& nameLower, const std::wstring& queryLower) {
		if (queryLower.empty()) return 0;
		if (nameLower.rfind(queryLower, 0) == 0) return 3;                  // prefix match
		if (nameLower.find(queryLower) != std::wstring::npos) return 2;     // substring match

		std::wistringstream words(queryLower);
		std::wstring word;
		bool anyWord = false;
		bool allFound = true;
		while (words >> word) {
			anyWord = true;
			if (nameLower.find(word) == std::wstring::npos) {
				allFound = false;
				break;
			}
		}
		return (anyWord && allFound) ? 1 : 0;
	}
}

std::vector<ProgramLauncher::InstalledProgram> ProgramLauncher::enumerate() {
	std::vector<InstalledProgram> results;

	wchar_t* programData = nullptr;
	size_t envLen = 0;
	if (_wdupenv_s(&programData, &envLen, L"ProgramData") == 0 && programData) {
		collectShortcuts(std::wstring(programData) + L"\\Microsoft\\Windows\\Start Menu\\Programs", results);
	}
	if (programData) free(programData);

	wchar_t* appData = nullptr;
	if (_wdupenv_s(&appData, &envLen, L"AppData") == 0 && appData) {
		collectShortcuts(std::wstring(appData) + L"\\Microsoft\\Windows\\Start Menu\\Programs", results);
	}
	if (appData) free(appData);

	return results;
}

std::vector<ProgramLauncher::InstalledProgram> ProgramLauncher::search(const std::wstring& query, int maxResults) {
	std::wstring queryLower = query;
	for (wchar_t& c : queryLower) c = (wchar_t)towlower(c);

	// Kept alive for the whole function (scored below points into it) - the copies that
	// actually leave this function are made explicitly, into results, further down.
	std::vector<InstalledProgram> all = enumerate();

	std::vector<std::pair<int, const InstalledProgram*>> scored;
	scored.reserve(all.size());
	for (const InstalledProgram& program : all) {
		std::wstring nameLower = program.displayName;
		for (wchar_t& c : nameLower) c = (wchar_t)towlower(c);

		int score = scoreMatch(nameLower, queryLower);
		if (score > 0) scored.emplace_back(score, &program);
	}

	// Stable, not just sorted - among equally-scored matches, keeps enumerate()'s own (roughly
	// filesystem-walk) order rather than an arbitrary reshuffle, so results are at least
	// deterministic run to run.
	std::stable_sort(scored.begin(), scored.end(),
		[](const std::pair<int, const InstalledProgram*>& a, const std::pair<int, const InstalledProgram*>& b) {
			return a.first > b.first;
		});

	std::vector<InstalledProgram> results;
	for (size_t i = 0; i < scored.size() && (int)results.size() < maxResults; i++) {
		results.push_back(*scored[i].second);
	}
	return results;
}

std::wstring ProgramLauncher::resolveShortcutTarget(const std::wstring& shortcutPath) {
	std::wstring result;

	IShellLinkW* psl = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl)) && psl) {
		IPersistFile* ppf = nullptr;
		if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
			if (SUCCEEDED(ppf->Load(shortcutPath.c_str(), STGM_READ))) {
				wchar_t targetPath[MAX_PATH]{};
				WIN32_FIND_DATAW findData{};
				if (SUCCEEDED(psl->GetPath(targetPath, MAX_PATH, &findData, SLGP_RAWPATH))) {
					result = targetPath;
				}
			}
			ppf->Release();
		}
		psl->Release();
	}

	return result;
}

bool ProgramLauncher::launchAndDetectWindow(const InstalledProgram& program, std::stop_token stoken,
	HWND& outHwnd, DWORD timeoutMs) {
	outHwnd = NULL;

	WindowScanner scanner;
	if (!scanner.initialize()) return false;

	// Baseline snapshot of every top-level window open right now, taken immediately before
	// launching - the newly-launched program's window is identified by diffing against this,
	// rather than trusting the launched process's own PID alone (many installers/launchers
	// relaunch through a short-lived stub process with a different PID than ShellExecuteExW
	// hands back).
	std::vector<ScannedUIElement> baselineElements = scanner.fetchAllOpenWindows();
	std::vector<HWND> baselineHandles;
	baselineHandles.reserve(baselineElements.size());
	for (const ScannedUIElement& elem : baselineElements) baselineHandles.push_back(elem.hwnd);

	// Best-effort name hint for disambiguating between multiple new windows, if more than one
	// appears during the poll below - an empty targetImageName just means "no preference, take
	// the first new window" (see the fallback below).
	std::wstring targetPath = resolveShortcutTarget(program.shortcutPath);
	std::wstring targetImageName;
	if (!targetPath.empty()) {
		size_t slash = targetPath.find_last_of(L"\\/");
		targetImageName = (slash == std::wstring::npos) ? targetPath : targetPath.substr(slash + 1);
	}

	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"open";
	sei.lpFile = program.shortcutPath.c_str();
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei)) return false;

	HWND fallbackHwnd = NULL;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

	while (!stoken.stop_requested() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		std::vector<ScannedUIElement> current = scanner.fetchAllOpenWindows();
		for (const ScannedUIElement& elem : current) {
			bool isNew = std::find(baselineHandles.begin(), baselineHandles.end(), elem.hwnd) == baselineHandles.end();
			if (!isNew) continue;

			if (fallbackHwnd == NULL) fallbackHwnd = elem.hwnd;   // first-ever new window - the fallback

			if (targetImageName.empty()) continue;   // no name preference to check against

			DWORD pid = 0;
			GetWindowThreadProcessId(elem.hwnd, &pid);
			HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (!hProc) continue;

			wchar_t imagePath[MAX_PATH]{};
			DWORD imagePathLen = MAX_PATH;
			bool matched = false;
			if (QueryFullProcessImageNameW(hProc, 0, imagePath, &imagePathLen)) {
				std::wstring imageName = imagePath;
				size_t slash = imageName.find_last_of(L"\\/");
				if (slash != std::wstring::npos) imageName = imageName.substr(slash + 1);
				matched = (_wcsicmp(imageName.c_str(), targetImageName.c_str()) == 0);
			}
			CloseHandle(hProc);

			if (matched) {
				if (sei.hProcess) CloseHandle(sei.hProcess);
				outHwnd = elem.hwnd;
				return true;
			}
		}
	}

	if (sei.hProcess) CloseHandle(sei.hProcess);

	if (fallbackHwnd != NULL) {
		outHwnd = fallbackHwnd;
		return true;
	}
	return false;
}
