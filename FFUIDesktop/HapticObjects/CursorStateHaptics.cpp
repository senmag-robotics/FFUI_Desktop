#include "CursorStateHaptics.h"
#include "../FFUIDesktop.h"   //for DEVICE_WORKSPACE_X/Y and FFUIDesktop::desktopConfig.screenSize -
                              //see computeTextLineDetentForce()'s own comment for why this is a
                              //.cpp-only include rather than something CursorStateHaptics.h itself
                              //could safely add.
#include <windows.h>
#include <oleauto.h>   //SafeArrayAccessData/GetLBound/GetUBound/UnaccessData/Destroy - needed by
                        //computeTextLineDetentForce()'s GetBoundingRectangles() read below; added
                        //defensively rather than assumed transitively available, same precedent as
                        //WindowScanner.cpp's own VariantInit()/VariantClear() include.
#include <algorithm>   //std::min/std::max, used by refreshLineCache()'s rect-union loop
#include <cmath>       //std::abs/std::copysign, used by axisLineDetentPull()

namespace {
	// All four states share the same shape (a periodic sine burst, same as SolidPlane's own
	// edge-buzz) - only the timing/amplitude differ per state, tuned "by feel" like every other
	// haptic constant in this codebase (see DetentTuning.h), so each one is distinguishable
	// from the others (and from the edge-buzz itself, at 100Hz/0.1s-burst/0.2s-repeat) purely
	// by touch. Adjust freely once tried on the real hardware.
	VibrationSettings makeBurstSettings(float frequency, float burstDuration, float repeatPeriod, float gainMax) {
		VibrationSettings settings{};
		settings.type = vibrationType_Periodic;
		settings.frequency = frequency;
		settings.offset = 0;
		settings.profile = vibrationProfile_sine;
		settings.modulation.type = modulationType_sine;
		settings.modulation.duration = burstDuration;
		settings.modulation.repeatPeriod = repeatPeriod;
		settings.modulation.gainMaximum = gainMax;
		settings.modulation.gainMinimum = 0;
		return settings;
	}
}

CursorStateHaptics::CursorStateHaptics() :
	// gainMax values below are half of this feature's first-pass defaults - "make them a bit
	// more subtle - maybe half the magnitude of all the ones we just added", per the request -
	// timing (frequency/burstDuration/repeatPeriod) left untouched, since "subtle" was about
	// strength, not the pattern each one is recognized by.
	//
	// Text field: fast, soft, tightly-repeated - reads as a near-continuous hum while
	// lingering over an editable field, distinct from the more clearly separated pulses below.
	textFieldVibration(makeBurstSettings(/*frequency*/ 60.0f, /*burstDuration*/ 0.08f, /*repeatPeriod*/ 0.10f, /*gainMax*/ 0.0005f)),
	// Clickable (hand, plain hyperlink/clickable text): a sharp, short, clearly separated tap -
	// meant to feel "clicky", like there's a discrete target here rather than a field to linger in.
	clickableVibration(makeBurstSettings(/*frequency*/ 40.0f, /*burstDuration*/ 0.05f, /*repeatPeriod*/ 0.20f, /*gainMax*/ 0.0009f)),
	// Clickable control (hand, but a real button/menu item/checkbox/etc. per UI Automation - see
	// classifyCurrentCursor()'s own comment): a firmer, more emphatic double-pulse at the same
	// pitch as plain Clickable above, so the two are still clearly kin (both "hand cursor, could
	// press it") but a genuine control reads as weightier than a plain link - unverified, tune by
	// feel once tried on hardware, like every other constant here.
	clickableControlVibration(makeBurstSettings(/*frequency*/ 40.0f, /*burstDuration*/ 0.05f, /*repeatPeriod*/ 0.10f, /*gainMax*/ 0.0013f)),
	// Resize handle: lower and heavier, near-continuous - meant to feel "draggable"/mechanical
	// rather than "clicky".
	resizeHandleVibration(makeBurstSettings(/*frequency*/ 20.0f, /*burstDuration*/ 0.15f, /*repeatPeriod*/ 0.18f, /*gainMax*/ 0.0006f)),
	// Unresponsive: slow and deliberate - a "heartbeat" cadence, clearly slower than every
	// other state here so it reads as "something's wrong/waiting" rather than a normal hover
	// affordance.
	unresponsiveVibration(makeBurstSettings(/*frequency*/ 8.0f, /*burstDuration*/ 0.25f, /*repeatPeriod*/ 0.6f, /*gainMax*/ 0.0010f))
{
}

// Real UIA button/menu-item/etc. control types that should read as the firmer ClickableControl
// buzz rather than plain Clickable - deliberately conservative (checkbox/radio/tab/list items
// alongside the obvious button/menu item), leaving anything else (in particular
// UIA_HyperlinkControlTypeId and UIA_TextControlTypeId) on the existing plain-Clickable texture.
// A local, haptics-only bucketing - distinct from (and coarser in the other direction than)
// WindowScanner::mapControlType()'s own UIElementType mapping, which lumps hyperlinks in with
// UIElementType::Button for narration/haptic-solid-force purposes; the two classifications serve
// different purposes and don't need to agree.
namespace {
	bool isControlTypeBucket(long controlTypeId) {
		return controlTypeId == UIA_ButtonControlTypeId
			|| controlTypeId == UIA_MenuItemControlTypeId
			|| controlTypeId == UIA_ListItemControlTypeId
			|| controlTypeId == UIA_TabItemControlTypeId
			|| controlTypeId == UIA_CheckBoxControlTypeId
			|| controlTypeId == UIA_RadioButtonControlTypeId;
	}
}

bool CursorStateHaptics::isControlTypeElementAtPoint(POINT screenPoint) {
	//See cursorUiaLookupsEnabled's own comment (CursorStateHaptics.h) - diagnostic bypass, skips
	//the UIA call entirely and falls back to the plain-Clickable answer while testing whether
	//this class's own haptic-thread UIA traffic is what's colliding with the scanner thread's.
	if (!cursorUiaLookupsEnabled) {
		return false;
	}

	//Throttled, new this round ("we're seeing quite some processor load") - see
	//uiaLookupThrottleFrames' own comment (CursorStateHaptics.h) for why. Every call still
	//advances the counter; only every uiaLookupThrottleFrames-th call actually re-issues the COM
	//lookup below, all others just hand back the last answer.
	int thisCall = controlTypeLookupCounter++;
	if (thisCall % uiaLookupThrottleFrames != 0) {
		return cachedIsControlType;
	}

	IUIAutomation* automation = GetSharedAutomation();  // see UiaShared.h for why this is shared/lazy
	if (!automation) {
		cachedIsControlType = false;
		return cachedIsControlType;
	}

	IUIAutomationElement* pElement = nullptr;
	if (FAILED(automation->ElementFromPoint(screenPoint, &pElement)) || !pElement) {
		cachedIsControlType = false;
		return cachedIsControlType;
	}

	CONTROLTYPEID controlTypeId = 0;
	cachedIsControlType = SUCCEEDED(pElement->get_CurrentControlType(&controlTypeId)) && isControlTypeBucket(controlTypeId);
	pElement->Release();
	return cachedIsControlType;
}

CursorHapticState CursorStateHaptics::classifyCurrentCursor() {
	CURSORINFO info{};
	info.cbSize = sizeof(CURSORINFO);
	if (!GetCursorInfo(&info) || info.hCursor == NULL) return CursorHapticState::None;

	// Comparing raw HCURSOR handles works reliably for every STOCK system cursor - Windows
	// shares one handle per stock cursor resource across every process that loads it via
	// LoadCursor(NULL, ...), so a genuine IDC_IBEAM anywhere on the system is always this same
	// handle. Loaded once (function-static) rather than every call, since these never change
	// for the lifetime of the process. A fully custom, app-drawn cursor (some browsers/games
	// skin their own I-beam or hand rather than using the OS stock one) simply won't match any
	// of these and falls through to None below - a silent, safe miss rather than a wrong
	// guess, matching this codebase's existing "best-effort, not guaranteed" tone
	// (ProgramLauncher's window detection, WhisperTranscriber's transcription timeout).
	static HCURSOR ibeam = LoadCursor(NULL, IDC_IBEAM);
	static HCURSOR hand = LoadCursor(NULL, IDC_HAND);
	static HCURSOR sizeNS = LoadCursor(NULL, IDC_SIZENS);
	static HCURSOR sizeWE = LoadCursor(NULL, IDC_SIZEWE);
	static HCURSOR sizeNWSE = LoadCursor(NULL, IDC_SIZENWSE);
	static HCURSOR sizeNESW = LoadCursor(NULL, IDC_SIZENESW);
	static HCURSOR sizeAll = LoadCursor(NULL, IDC_SIZEALL);
	static HCURSOR wait = LoadCursor(NULL, IDC_WAIT);
	static HCURSOR appStarting = LoadCursor(NULL, IDC_APPSTARTING);

	if (info.hCursor == ibeam) return CursorHapticState::TextField;
	if (info.hCursor == hand) {
		// Only reached when the cursor is already hand-shaped, so this UIA lookup runs on a
		// small minority of frames, not every frame - see isControlTypeElementAtPoint()'s own
		// comment for the split this decides between.
		return isControlTypeElementAtPoint(info.ptScreenPos)
			? CursorHapticState::ClickableControl
			: CursorHapticState::Clickable;
	}
	if (info.hCursor == sizeNS || info.hCursor == sizeWE ||
		info.hCursor == sizeNWSE || info.hCursor == sizeNESW ||
		info.hCursor == sizeAll) return CursorHapticState::ResizeHandle;
	// IDC_WAIT (the plain hourglass/ring) and IDC_APPSTARTING (arrow-plus-hourglass, shown
	// while a window's message queue is slow to pump messages) are both treated as
	// "unresponsive" here - per the request, inferred purely from the cursor shape rather than
	// the more precise (but explicitly declined) IsHungAppWindow() check, so this will also
	// fire during ordinary brief busy moments, not only a genuine hang.
	if (info.hCursor == wait || info.hCursor == appStarting) return CursorHapticState::Unresponsive;

	return CursorHapticState::None;
}

// The actual UIA lookup - see this method's own declaration comment (CursorStateHaptics.h) for
// why it's split out of computeTextLineDetentForce() below. Best-effort throughout, same tone as
// every other UIA consumer in this codebase: any failure along the way (no TextPattern support,
// RangeFromPoint missing, an empty/degenerate bounding-rectangle array) simply returns false
// (leaving the out-params untouched) rather than propagating an error.
bool CursorStateHaptics::refreshLineCache(Vector3& outCenterWorkspace, Vector2& outHalfExtentWorkspace) {
	IUIAutomation* automation = GetSharedAutomation();
	if (!automation) return false;

	CURSORINFO info{};
	info.cbSize = sizeof(CURSORINFO);
	if (!GetCursorInfo(&info)) return false;

	IUIAutomationElement* pElement = nullptr;
	if (FAILED(automation->ElementFromPoint(info.ptScreenPos, &pElement)) || !pElement) return false;

	//Same ancestor-walk NarrateFlow's own tryReadTextLineAtCursor() uses (UiaShared.h) - a raw
	//hit test very often lands on an inner leaf (a single text run/span) that doesn't itself
	//implement TextPattern, even though the actual edit/document control containing it does.
	IUIAutomationTextPattern* pTextPattern = FindTextPatternFromElementOrAncestors(automation, pElement);
	pElement->Release();
	if (!pTextPattern) return false;

	IUIAutomationTextRange* pRange = nullptr;
	HRESULT hr = pTextPattern->RangeFromPoint(info.ptScreenPos, &pRange);
	pTextPattern->Release();
	if (FAILED(hr) || !pRange) return false;

	//Same TextUnit_Line granularity NarrateFlow::tryReadTextLineAtCursor() already reads for
	//narration - "the narrator that only reads the hovered line" - so what's felt and what's
	//read agree on the same unit.
	pRange->ExpandToEnclosingUnit(TextUnit_Line);

	SAFEARRAY* pRects = nullptr;
	hr = pRange->GetBoundingRectangles(&pRects);
	pRange->Release();
	if (FAILED(hr) || !pRects) return false;

	double* data = nullptr;
	if (FAILED(SafeArrayAccessData(pRects, (void**)&data)) || !data) {
		SafeArrayDestroy(pRects);
		return false;
	}

	//4 doubles per fragment (x, y, width, height) - a wrapped or bidi line can rarely produce
	//more than one visual fragment; union their bounding boxes into one rect rather than only
	//looking at the first, so the computed center is still meaningful in that rare case.
	long lBound = 0, uBound = -1;
	SafeArrayGetLBound(pRects, 1, &lBound);
	SafeArrayGetUBound(pRects, 1, &uBound);
	long fragmentCount = (uBound - lBound + 1) / 4;

	bool haveRect = false;
	double unionLeft = 0, unionTop = 0, unionRight = 0, unionBottom = 0;
	for (long i = 0; i < fragmentCount; ++i) {
		double x = data[i * 4 + 0];
		double y = data[i * 4 + 1];
		double w = data[i * 4 + 2];
		double h = data[i * 4 + 3];
		if (w <= 0 || h <= 0) continue;   //degenerate fragment - skip

		if (!haveRect) {
			unionLeft = x; unionTop = y; unionRight = x + w; unionBottom = y + h;
			haveRect = true;
		}
		else {
			unionLeft = (std::min)(unionLeft, x);
			unionTop = (std::min)(unionTop, y);
			unionRight = (std::max)(unionRight, x + w);
			unionBottom = (std::max)(unionBottom, y + h);
		}
	}

	SafeArrayUnaccessData(pRects);
	SafeArrayDestroy(pRects);

	if (!haveRect) return false;

	Vector2 lineCenterScreen(
		static_cast<float>((unionLeft + unionRight) / 2.0),
		static_cast<float>((unionTop + unionBottom) / 2.0));
	float halfWidthScreen = static_cast<float>((unionRight - unionLeft) / 2.0);
	float halfHeightScreen = static_cast<float>((unionBottom - unionTop) / 2.0);

	//Same formula as ObjectFactory::screenToWorkspace() (ObjectsFactory.cpp), duplicated here
	//rather than shared via an include - see this class's own header comment for why
	//ObjectsFactory.h specifically isn't included from CursorStateHaptics.h/.cpp (a genuine
	//circular-include risk: FFUIDesktop.h includes CursorStateHaptics.h, and ObjectsFactory.h
	//in turn includes FFUIDesktop.h). zPosition matches ObjectsFactory.cpp's own default:
	//branch (215) - the same depth every scanned Button/ListItem/MenuItem element (including
	//the very ButtonObject this line-detent is meant to take over from - see
	//FFUIDesktop::processForces()'s own suppressButtonDetents) already sits at, so the two
	//compete for the stylus on a level footing rather than one being nearer/farther by
	//construction.
	Vector2 screenSize = FFUIDesktop::desktopConfig.screenSize;
	const float zPosition = 215.0f;
	float centerX = (lineCenterScreen.x / screenSize.x - 0.5f) * DEVICE_WORKSPACE_X;
	float centerY = (0.5f - lineCenterScreen.y / screenSize.y) * DEVICE_WORKSPACE_Y;
	outCenterWorkspace = Vector3(centerX, centerY, zPosition);

	//Magnitude-only conversion (no 0.5f offset/flip needed - a width/height is already a plain
	//extent, not a position), same DEVICE_WORKSPACE_X/Y scale factors as the center conversion
	//just above.
	outHalfExtentWorkspace = Vector2(
		halfWidthScreen / screenSize.x * DEVICE_WORKSPACE_X,
		halfHeightScreen / screenSize.y * DEVICE_WORKSPACE_Y);

	return true;
}

//A single independent axis of the line detent - zero inside deadzoneFraction*halfExtent of the
//line's own center on that axis, then ramping LINEARLY from zero up to forceLimit exactly at the
//line's own edge (halfExtent) - so the "notch" always spans the line's own real on-screen size
//(whatever the current font/zoom happens to be) rather than a fixed guessed-at distance, and the
//pull is at its strongest right as the stylus is about to cross into the next line, which is what
//actually makes each line-to-line crossing read as a distinct detent rather than a vague blur -
//see lineDetentDeadzoneFractionY/lineDetentForceLimitY's own comment (CursorStateHaptics.h) for
//why Y and X are tuned very differently.
static float axisLineDetentPull(float towardCenter, float halfExtent, float deadzoneFraction, float forceLimit) {
	if (halfExtent <= 0.0f) return 0.0f;

	float deadzone = halfExtent * deadzoneFraction;
	float absOffset = std::abs(towardCenter);
	if (absOffset <= deadzone) return 0.0f;

	float span = (std::max)(halfExtent - deadzone, 0.001f);
	float t = (std::min)((absOffset - deadzone) / span, 1.0f);
	return std::copysign(t * forceLimit, towardCenter);
}

// New this round - see this method's own declaration comment (CursorStateHaptics.h) for the
// motivation, and axisLineDetentPull()'s own comment for the per-axis spring shape. The expensive
// UIA half (refreshLineCache()) only actually runs every uiaLookupThrottleFrames haptic frames -
// see that constant's own comment; every other frame just reuses whichever line geometry was
// last found and recomputes the (cheap) spring force fresh against currentLoc, so the output
// stays smooth even though the lookup behind it updates less often.
Vector3 CursorStateHaptics::computeTextLineDetentForce(Location currentLoc) {
	//See cursorUiaLookupsEnabled's own comment (CursorStateHaptics.h) - diagnostic bypass, skips
	//refreshLineCache()'s UIA call entirely (no line-detent pull at all while this is false)
	//while testing whether this class's own haptic-thread UIA traffic is what's colliding with
	//the scanner thread's.
	if (!cursorUiaLookupsEnabled) {
		return Vector3(0, 0, 0);
	}

	bool shouldRefresh = (lineLookupCounter % uiaLookupThrottleFrames) == 0;
	lineLookupCounter++;

	if (shouldRefresh) {
		haveCachedLine = refreshLineCache(cachedLineCenterWorkspace, cachedLineHalfExtentWorkspace);
	}

	if (!haveCachedLine) return Vector3(0, 0, 0);

	float towardCenterX = cachedLineCenterWorkspace.x - currentLoc.position.x;
	float towardCenterY = cachedLineCenterWorkspace.y - currentLoc.position.y;

	Vector3 force(
		axisLineDetentPull(towardCenterX, cachedLineHalfExtentWorkspace.x, lineDetentDeadzoneFractionX, lineDetentForceLimitX),
		axisLineDetentPull(towardCenterY, cachedLineHalfExtentWorkspace.y, lineDetentDeadzoneFractionY, lineDetentForceLimitY),
		0);   //X/Y pull only - matches ButtonObject::calculateInteractionForce()'s own force.z = 0
	          //convention; the Z-axis buzz stays entirely update()'s job.

	return force;
}

Vector3 CursorStateHaptics::update(Location currentLoc) {
	Vector3 force(0, 0, 0);
	CursorHapticState state = classifyCurrentCursor();

	//Force an immediate (not throttled) line lookup the moment the cursor freshly becomes
	//TextField, rather than possibly reusing a stale cachedLineCenterWorkspace left over from
	//wherever text was last hovered (a different window, a different line, possibly a while
	//ago) - uiaLookupThrottleFrames is about not re-querying every single frame WHILE lingering
	//in one place, not about tolerating a stale result right at the moment you land somewhere
	//new. Resetting the counter to exactly 0 here guarantees computeTextLineDetentForce()'s own
	//modulo check refreshes on this very tick.
	if (state == CursorHapticState::TextField && lastClassifiedState != CursorHapticState::TextField) {
		lineLookupCounter = 0;
	}

	//Read back by FFUIDesktop::updateFrame() via isCursorOverTextField() this same tick - see
	//that method's own comment.
	lastClassifiedState = state;

	// Blended onto force.z specifically, per Gareth's request that every one of these cursor-state
	// textures act entirely on Z, with no X/Y component at all - deliberately DIFFERENT from
	// SolidPlane's own edge-buzz (still Y - see SolidPlane.cpp, untouched by this) and from the
	// per-object pull/detent forces (X/Y), so this class's own contribution to the per-tick total
	// never adds any sideways pull of its own, only an in/out-of-the-desk-plane buzz layered on
	// top of whatever else is happening on X/Y.
	if (state == CursorHapticState::TextField) force.z += textFieldVibration.processVibration();
	else textFieldVibration.sleepVibration();

	if (state == CursorHapticState::Clickable) force.z += clickableVibration.processVibration();
	else clickableVibration.sleepVibration();

	if (state == CursorHapticState::ClickableControl) force.z += clickableControlVibration.processVibration();
	else clickableControlVibration.sleepVibration();

	if (state == CursorHapticState::ResizeHandle) force.z += resizeHandleVibration.processVibration();
	else resizeHandleVibration.sleepVibration();

	if (state == CursorHapticState::Unresponsive) force.z += unresponsiveVibration.processVibration();
	else unresponsiveVibration.sleepVibration();

	//New this round: while over real text, also pull X/Y toward the current LINE's own center -
	//see computeTextLineDetentForce()'s own comment. The Z buzz above is untouched either way.
	if (state == CursorHapticState::TextField) {
		Vector3 lineForce = computeTextLineDetentForce(currentLoc);
		force.x += lineForce.x;
		force.y += lineForce.y;
	}

	return force;
}
