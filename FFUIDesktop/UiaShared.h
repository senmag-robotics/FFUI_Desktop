#pragma once

#include <windows.h>
#include <UIAutomation.h>

//The one shared IUIAutomation instance for the haptic thread - CursorStateHaptics (cursor-shape
//haptic classification) and NarrateFlow (Narrate's third-party element/text reading) both need
//their own IUIAutomation to call ElementFromPoint()/etc. against, and rather than each lazily
//creating its own COM object independently (as CursorStateHaptics originally did on its own),
//this factors that into one place both can share - see NARRATE_SPEC.md's own "Third-party UIA
//scope" section for why this plumbing keeps growing more consumers.
//
//Lazily created on first call (a function-local static, so it's created exactly once, the first
//time any caller actually needs it) rather than at process start - both current callers are
//members of objects constructed before FFUIDesktop::initDesktop()'s CoInitialize() has run (see
//main.cpp: `FFUIDesktop ffuiDesktop;` runs before `ffuiDesktop.initDesktop(...)`), so an
//eager/constructor-time CoCreateInstance would fail with CO_E_NOTINITIALIZED. By the time either
//caller is ever actually invoked (only from FFUIDesktop::updateFrame(), which only runs after
//initDesktop() completes), COM on this thread is guaranteed initialized - mirrors
//WindowScanner::initialize()'s own CoCreateInstance(CLSID_CUIAutomation, ...) call exactly, just
//deferred to first use instead of an explicit initialize() call, and shared rather than
//per-instance.
//
//Must only be called from the haptic thread (the same thread FFUIDesktop::initDesktop() and
//FFUIDesktop::updateFrame() both run on - see main.cpp) - this is not itself thread-safe against
//being called from a second thread for the first time concurrently, which neither current caller
//does (both are haptic-thread-only, same as CursorStateHaptics::update()/NarrateFlow::readCurrent()
//themselves). Never released - lives for the process's lifetime, same as pSapiVoice/
//pAssistantVoice/pNarrateVoice in FFUIDesktop.h.
inline IUIAutomation* GetSharedAutomation() {
	static IUIAutomation* pAutomation = [] {
		IUIAutomation* p = nullptr;
		CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
			__uuidof(IUIAutomation), (void**)&p);
		return p;
	}();
	return pAutomation;
}

//A raw screen-point hit test (ElementFromPoint) very often lands on an inner leaf - a single text
//run/span, an inline image, a generic layout div - that itself doesn't implement TextPattern,
//even though the actual edit/document control containing it does. Walks up via RawViewWalker from
//pElement (inclusive) looking for the first ancestor that supports TextPattern, bounded at
//maxAncestors hops so a pathological/very deep tree (a heavily-nested web page) can't turn one
//lookup into a long walk on the haptic thread. Returns an AddRef'd pattern the caller must
//Release(), or nullptr if none found within the bound - best-effort, same tone as everywhere else
//UIA is consulted in this codebase.
//
//Originally NarrateFlow.cpp's own local helper (built for "still a lot of text fields that aren't
//picked up by the narrator - e.g. in emails"); moved here once CursorStateHaptics needed the exact
//same walk for its own per-line detent (see that class's own comment) - same sharing rationale as
//GetSharedAutomation() just above.
inline IUIAutomationTextPattern* FindTextPatternFromElementOrAncestors(IUIAutomation* automation, IUIAutomationElement* pElement) {
	IUIAutomationTreeWalker* pWalker = nullptr;
	if (FAILED(automation->get_RawViewWalker(&pWalker)) || !pWalker) return nullptr;

	IUIAutomationElement* current = pElement;
	current->AddRef();  //so every iteration below can uniformly Release() what it holds, without
	                     //touching the caller's own reference to pElement.

	const int maxAncestors = 10;
	IUIAutomationTextPattern* found = nullptr;

	for (int i = 0; i < maxAncestors && current; ++i) {
		IUIAutomationTextPattern* pattern = nullptr;
		if (SUCCEEDED(current->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern), (void**)&pattern)) && pattern) {
			found = pattern;
			current->Release();
			current = nullptr;
			break;
		}

		IUIAutomationElement* parent = nullptr;
		pWalker->GetParentElement(current, &parent);
		current->Release();
		current = parent;
	}

	if (current) current->Release();
	pWalker->Release();
	return found;
}
