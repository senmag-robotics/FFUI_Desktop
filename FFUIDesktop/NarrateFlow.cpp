#include "NarrateFlow.h"
#include "FFUIDesktop.h"   //for pNarrateVoice
#include "ButtonObject.h"
#include "GridTileObject.h"
#include "WindowWallObject.h"
#include "UiaShared.h"
#include "VoiceSettingsManager.h"   //for getFfuiRate() - Narrate's own speed inherits FFUI's
#include <windows.h>
#include <UIAutomation.h>
#include <iostream>

namespace {
	//Naive narrow->wide widen (each byte to its own wchar_t) - matches this codebase's existing
	//tolerance for lossy narrow/wide round-tripping of UI element names (customName itself is
	//already populated from a wide UIA name truncated to narrow in
	//ObjectFactory::createObjectsFromUIElements() - see that call site's own
	//std::string(elem.name.begin(), elem.name.end())), so this doesn't introduce any new loss
	//beyond what already happens before this function ever sees the name. Fine for the
	//overwhelmingly-ASCII app/button names this speaks in practice.
	std::wstring widenNarrow(const std::string& narrow) {
		return std::wstring(narrow.begin(), narrow.end());
	}

	//Spoken role for a scanned third-party element, keyed off the same UIElementType every
	//ButtonObject already carries in its own objectMeta.uiType - see WIndowScanner.h's enum and
	//ObjectsFactory.cpp's mapControlType()/createObjectsFromUIElements() for where this is set.
	//UIA_HyperlinkControlTypeId already maps to UIElementType::Button upstream (mapControlType()),
	//so a hyperlink is spoken as "button" here too - a coarser distinction than CursorStateHaptics'
	//own Clickable/ClickableControl haptic split (see that class's own comment), acceptable for
	//this stage since NARRATE_SPEC.md's Name/Role/State convention only asks for a role, not a
	//haptic-grade classification.
	std::wstring roleForType(UIElementType type) {
		switch (type) {
		case UIElementType::Button:     return L"button";
		case UIElementType::ListItem:   return L"list item";
		case UIElementType::MenuItem:   return L"menu item";
		case UIElementType::TextField:  return L"text field";
		case UIElementType::ScrollBar:  return L"scroll bar";
		default:                        return L"item";
		}
	}

	//The one place that actually calls pNarrateVoice->Speak() - mirrors to the console the same
	//way every other narrator in this codebase does ("mirror all the narrator requests to the
	//console please", per the request that first established that convention - see
	//VoiceAssistant.cpp's own [Narrate:Assistant] tag). Tagged [Narrate] here, distinct from
	//[Narrate:Assistant], so the two are easy to tell apart in the console log.
	//
	//Sets pNarrateVoice's rate from VoiceSettingsManager::getFfuiRate() immediately before every
	//utterance, rather than once at startup - "make sure the narrator inherits speed settings
	//from the FFUI narrator", per the request. Unlike TutorialFlow's own borrow-then-restore of
	//this exact same rate on pAssistantVoice (see TutorialFlow.cpp), pNarrateVoice has no
	//independent rate of its own to restore afterward - it has no "Settings: Narrate" tile/rate
	//setting today, so simply tracking pSapiVoice's rate on every utterance keeps it always
	//current with whatever the user has set FFUI's own narrator speed to, including a change
	//made after the app started. Cheap - SetRate() is a plain property set, not worth caching.
	void speakThroughNarrateVoice(const std::wstring& line) {
		std::string lineNarrow(line.begin(), line.end());
		std::cout << "[Narrate] \"" << lineNarrow << "\"" << std::endl;

		if (pNarrateVoice) {
			pNarrateVoice->SetRate(VoiceSettingsManager::getInstance().getFfuiRate());
			pNarrateVoice->Speak(line.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
		}
	}

	//Formats "Name, Role[, State]" (or "Unnamed {role}" if the scanned element had no accessible
	//name) and speaks it via speakThroughNarrateVoice() above.
	void speakNarration(const std::wstring& name, const std::wstring& role, const std::wstring& state) {
		std::wstring line;
		if (name.empty()) {
			line = L"Unnamed " + role;
		}
		else {
			line = name + L", " + role;
		}
		if (!state.empty()) {
			line += L", " + state;
		}
		speakThroughNarrateVoice(line);
	}

	//Reads the current LINE of real text (an email body, a Word document, any other editable or
	//selectable text content) at the real OS cursor's current screen position, via UI Automation's
	//Text pattern - see NARRATE_SPEC.md's own "Bounding long text" section for why "current line"
	//is the chosen unit (matches NVDA's own default review-cursor granularity). This is
	//deliberately independent of the ButtonObject/GridTileObject/WindowWallObject scan below: text
	//content (the actual words in a document) isn't a Name the way a button's accessible name is -
	//it has to be read from the element's Value/Text pattern instead, at the exact point the
	//cursor is hovering, not from anything already carried on a scanned ScannedUIElement/ButtonObject
	//(which only ever carried a Name - see WIndowScanner.h). Hardware-test feedback: "the narrator
	//seems to generally work well on buttons/UI elements, but not on text fields (e.g. reading an
	//email or Word document)" - this is what that gap was actually missing.
	//
	//Only meaningful while WindowManager::isControllingActiveRoom is true (see this class's own
	//gating in readCurrent()) - that's the only time the real OS cursor is actually sitting where
	//the stylus put it, mirroring CursorStateHaptics's own identical gating comment. Best-effort,
	//same tone as everywhere else UIA is consulted in this codebase: ElementFromPoint failing, the
	//element not supporting TextPattern at all (a plain button, most custom-drawn UI), or an empty
	//resulting line all simply return false so readCurrent() falls through to the ordinary
	//Name/Role/State scan below instead.
	bool tryReadTextLineAtCursor(std::wstring& outLine) {
		IUIAutomation* automation = GetSharedAutomation();
		if (!automation) return false;

		CURSORINFO info{};
		info.cbSize = sizeof(CURSORINFO);
		if (!GetCursorInfo(&info)) return false;

		IUIAutomationElement* pElement = nullptr;
		if (FAILED(automation->ElementFromPoint(info.ptScreenPos, &pElement)) || !pElement) return false;

		//Walks up from the hit-tested element to find the first ancestor that actually supports
		//TextPattern, rather than trusting the raw hit test's own leaf element - see this shared
		//helper's own comment (UiaShared.h) for why (this is the actual fix for "text fields in
		//emails" not being read).
		IUIAutomationTextPattern* pTextPattern = FindTextPatternFromElementOrAncestors(automation, pElement);
		pElement->Release();
		if (!pTextPattern) return false;

		IUIAutomationTextRange* pRange = nullptr;
		HRESULT hr = pTextPattern->RangeFromPoint(info.ptScreenPos, &pRange);
		pTextPattern->Release();
		if (FAILED(hr) || !pRange) return false;

		pRange->ExpandToEnclosingUnit(TextUnit_Line);
		BSTR text = nullptr;
		hr = pRange->GetText(-1, &text);
		pRange->Release();
		if (FAILED(hr) || !text) return false;

		outLine.assign(text, SysStringLen(text));
		SysFreeString(text);

		//ExpandToEnclosingUnit(TextUnit_Line) commonly includes the trailing line break in the
		//returned text - trimmed so it isn't heard as an awkward pause/silence at the end.
		while (!outLine.empty() && (outLine.back() == L'\r' || outLine.back() == L'\n')) {
			outLine.pop_back();
		}

		return !outLine.empty();
	}
}

void NarrateFlow::readCurrent(std::mutex& objectsListMutex, const std::vector<std::unique_ptr<FFUIObject>>& objects) {
	//Highest priority: real text content (an email, a Word document, any editable/selectable
	//text) at the real OS cursor's current position - see tryReadTextLineAtCursor()'s own comment
	//for why this has to be a separate, independent check rather than something the scan below
	//can answer. Gated on isControllingActiveRoom the same way CursorStateHaptics gates its own
	//cursor-position reads - outside that state the real OS cursor isn't sitting where the stylus
	//put it, so a lookup there wouldn't mean anything.
	if (WindowManager::getInstance().isControllingActiveRoom) {
		std::wstring textLine;
		if (tryReadTextLineAtCursor(textLine)) {
			speakThroughNarrateVoice(textLine);
			return;
		}
	}

	std::wstring name;
	std::wstring role;
	std::wstring state;
	bool found = false;

	{
		std::lock_guard<std::mutex> lock(objectsListMutex);

		//Next priority: a third-party button/list item/menu item from the currently-focused
		//window - see this class's own header comment for why these come first (otherwise
		//entirely silent today).
		for (const auto& object : objects) {
			ButtonObject* button = dynamic_cast<ButtonObject*>(object.get());
			if (button && button->stylusIsOnThis()) {
				name = widenNarrow(button->getMeta().customName);
				role = roleForType(button->getMeta().uiType);
				if (button->getMeta().isDisabled) {
					state = L"disabled";
				}
				found = true;
				break;
			}
		}

		//Second priority: an FFUI Settings tile.
		if (!found) {
			for (const auto& object : objects) {
				GridTileObject* tile = dynamic_cast<GridTileObject*>(object.get());
				if (tile && tile->stylusIsOnThis()) {
					name = tile->getLabel();
					role = L"settings tile";
					found = true;
					break;
				}
			}
		}

		//Third priority: a Program Tray entry, or the active slot's own window.
		if (!found) {
			for (const auto& object : objects) {
				WindowWallObject* window = dynamic_cast<WindowWallObject*>(object.get());
				if (window && window->stylusIsOnThis()) {
					name = WindowManager::getShortAppName(window->getHandle());
					role = window->isArchived() ? L"program tray item" : L"active slot";
					found = true;
					break;
				}
			}
		}
	}

	if (!found) {
		//Deliberately spoken, not silent - see this class's own header comment.
		speakThroughNarrateVoice(L"Nothing here right now.");
		return;
	}

	speakNarration(name, role, state);
}
