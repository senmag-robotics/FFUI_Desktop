#pragma once

#include "HapticVibration.h"
#include "../mathTypes.h"
#include "../UiaShared.h"
#include <windows.h>
#include <UIAutomation.h>

// Which real OS cursor shape (if any) is currently being shown, as classified by
// classifyCurrentCursor() in the .cpp - see that function's own comment for exactly which
// stock system cursor each one matches.
//
// ClickableControl is a further split of what used to be a single Clickable bucket - both a real
// button/menu item and a plain hyperlink commonly show the same IDC_HAND cursor, so cursor shape
// alone can't tell them apart. See classifyCurrentCursor()'s own comment for how the split is
// actually made (a UI Automation lookup, done only in the frames where the cursor is already
// hand-shaped, piggy-backing on the UIA plumbing NARRATE_SPEC.md brings into this codebase
// anyway) and NARRATE_SPEC.md's own "Haptics" section for the request this implements.
enum class CursorHapticState {
	None,
	TextField,
	Clickable,
	ClickableControl,
	ResizeHandle,
	Unresponsive
};

// Generates a distinct little vibration "texture" for whatever the real OS cursor is currently
// hovering, the same way SolidPlane already generates one for the workspace's own edges (see
// HapticVibration.h/SolidPlane.cpp) - "read this state from the cursor and generate unique
// vibration effects accordingly", per the request. Reuses HapticVibration exactly as-is (no
// changes needed there - it's already a self-contained, reusable oscillator/envelope
// generator); this class is just a thin cursor-shape classifier plus one persistent
// HapticVibration instance per recognized state.
//
// Deliberately reads the REAL Win32 cursor (GetCursorInfo()), not anything FFUI already tracks
// about its own virtual objects - "treat it as a keyboard" for dictation (see
// FFUIDesktop::updateFrame()'s own comment) and this cursor-state buzz are siblings in spirit:
// both let the OS-level state of whatever program you've popped through into show up on the
// device, on top of FFUI's own object model, rather than being limited to what FFUI itself
// already models about that program's UI.
//
// Only meaningful, and only ever called, while WindowManager::isControllingActiveRoom is true -
// see FFUIDesktop::updateFrame()'s call site - since that's the only time the real OS cursor is
// actually sitting somewhere the stylus put it, rather than wherever it was last left idle.
class CursorStateHaptics {
public:
	CursorStateHaptics();

	// Classifies the current OS cursor shape and returns this tick's resulting force, already
	// folded into one Vector3 ready to be added directly onto the per-tick force total: the
	// existing per-state vibration "texture" on Z (unchanged - see this method's own comment in
	// the .cpp), PLUS, new this round, a real X/Y pull-to-center-of-the-current-LINE detent
	// while state is TextField - see computeTextLineDetentForce()'s own comment for why. Every
	// recognized vibration state OTHER than whichever is current has its own HapticVibration put
	// to sleep this same tick (mirrors SolidPlane's "not touching -> sleep" branch), so switching
	// between states - or back to an unrecognized/plain-arrow cursor - never leaves a stale
	// vibration still running. currentLoc is the stylus's own current device/workspace position -
	// needed (new this round) to compute the line-detent's spring force; the vibration-only half
	// of this method never used it.
	Vector3 update(Location currentLoc);

	//True exactly when this tick's update() classified the cursor as TextField - i.e. a
	//computeTextLineDetentForce() pull is what's driving X/Y this tick, not the ordinary scanned-
	//object detents. FFUIDesktop::updateFrame() reads this (see its own comment) to skip the
	//normal per-object UI-force pass entirely while it's true: a scanned Button/ListItem/MenuItem
	//element can end up sitting at the exact same screen position as real text (most concretely,
	//Word apparently exposes each paragraph as a UIA list-item-shaped element for its own
	//accessibility purposes - see NARRATE_SPEC.md/hardware-test feedback, "playing with a word
	//doc, it feels as though each paragraph has a detent, pulling towards the middle - this
	//doesn't play well with the narrator that only reads the hovered line"), and that whole-
	//paragraph detent fighting this class's own line-sized one is exactly what made individual
	//lines hard to hover. Only meaningful for the same one tick update() was just called on -
	// call it right after update(), same as any other "read this tick's result" pattern.
	bool isCursorOverTextField() const { return lastClassifiedState == CursorHapticState::TextField; }

private:
	CursorHapticState classifyCurrentCursor();

	//Only called from the hand-cursor branch of classifyCurrentCursor() - see that method's own
	//comment for why a real button/menu item is distinguished from a plain hyperlink/clickable
	//text this way rather than by cursor shape alone. Best-effort: any failure (ElementFromPoint
	//failing, no automation instance, an app with a broken/incomplete UIA tree) simply falls back
	//to the plain Clickable bucket, matching this class's own established "safe miss rather than
	//a wrong guess" tone (see classifyCurrentCursor()'s custom-cursor comment).
	//
	//Throttled internally, new this round ("we're seeing quite some processor load") - see
	//uiaLookupThrottleFrames' own comment for why. Reuses cachedIsControlType on every call in
	//between refreshes rather than re-issuing the ElementFromPoint()/get_CurrentControlType() COM
	//round trip on every single one of the up to 500 haptic frames a second this can be called on.
	bool isControlTypeElementAtPoint(POINT screenPoint);

	//TEMPORARY DIAGNOSTIC TOGGLE - hardware-reported "briefly freezes every second or so, all
	//the time, not just on button presses". Disabling the scanner thread entirely already ruled
	//IN the scanner as involved; disabling just its button-triggered manual-rescan path did NOT
	//fix it (the ambient scan alone, unconditionally every ~500ms regardless of interaction, was
	//apparently already enough) - so the remaining, still-untested half of the working theory is
	//this class's OWN UI Automation traffic, made directly from the HAPTIC thread
	//(isControlTypeElementAtPoint() and refreshLineCache(), both below): both are full COM round
	//trips into whichever application the cursor is over, and UI Automation calls that reach a
	//target application ultimately execute on THAT application's own UI thread, regardless of
	//which of our own threads or COM apartments issued them - so this class's calls and the
	//scanner thread's own UIA walk, aimed at the same application, can queue up behind it
	//together even though neither of FFUI's own threads is waiting on the other's lock. With
	//this false, isControlTypeElementAtPoint() and computeTextLineDetentForce() both skip their
	//UIA calls entirely and return their "found nothing" answer immediately - cursor-shape
	//classification (GetCursorInfo, plain Win32, no COM) and the Z-axis vibration textures are
	//UNAFFECTED either way; only the button-vs-plain-link distinction and the per-line X/Y detent
	//pull are lost while this is off.
	//
	//RULED OUT - "nope, the stutter is still there" even with this false (and the scanner running
	//completely normally). Flipped back to true: this class's own UIA calls aren't the other half
	//of whatever's happening, so there's no reason to keep giving up the button-classification/
	//line-detent features while chasing a cause that isn't here. See scannerThreadEnabled's own
	//comment (FFUIDesktop.h) for the next theory being tested (thread scheduling priority, not a
	//UI Automation collision at all) and FFUIDesktop.cpp/main.cpp for the actual change.
	static constexpr bool cursorUiaLookupsEnabled = true;

	//New this round - the actual fix for "each paragraph has a detent... can we adjust so each
	//line has its own detent". Independent of, and takes priority over (see
	//isCursorOverTextField()'s own comment), the ordinary scanned-object ButtonObject/GridTile
	//detents: looks up the real text LINE at the OS cursor's current screen position via UI
	//Automation's TextPattern (same TextUnit_Line granularity NarrateFlow's own
	//tryReadTextLineAtCursor() already reads for narration - "the narrator that only reads the
	//hovered line" - so what you feel and what gets read now agree on the same unit), and pulls
	//the stylus toward that line's own on-screen center the same way ButtonObject pulls toward a
	//button's center - just sized to one line instead of a whole (possibly multi-line) scanned
	//element. Best-effort: any failure (no TextPattern support, RangeFromPoint failing, an empty
	//bounding-rectangle array) simply returns zero force, same tone as everywhere else UIA is
	//consulted in this codebase.
	//
	//The actual UIA lookup (ElementFromPoint -> ancestor walk -> RangeFromPoint ->
	//GetBoundingRectangles) is throttled the same way isControlTypeElementAtPoint() is - see
	//uiaLookupThrottleFrames' own comment - and only re-run every uiaLookupThrottleFrames haptic
	//frames; the (cheap, pure-math) spring force itself is still recomputed fresh every single
	//frame against currentLoc, using whichever line geometry (cachedLineHalfExtentWorkspace/
	//cachedLineCenterWorkspace) was most recently looked up, so the force output stays smooth and
	//continuous even though the expensive part behind it updates less often.
	Vector3 computeTextLineDetentForce(Location currentLoc);

	//The actual UIA lookup half of computeTextLineDetentForce() above, split out purely so it can
	//use ordinary early-returns on failure (matching this codebase's usual best-effort style)
	//without deeply nesting computeTextLineDetentForce() itself. Writes fresh workspace-space
	//center/half-extent into the two out-params and returns true on a genuine hit; returns false
	//(leaving the out-params untouched) on any failure. Only ever called from
	//computeTextLineDetentForce() on the ticks uiaLookupThrottleFrames says to actually refresh.
	bool refreshLineCache(Vector3& outCenterWorkspace, Vector2& outHalfExtentWorkspace);

	//How often (in haptic frames, at desktopConfig.targetHapticFramerate - 500 today, so every 15
	//frames is ~33 times a second) the two UIA lookups above actually re-run, rather than reusing
	//their last cached result - new this round ("we're seeing quite some processor load"): both
	//are a full UI Automation COM round trip (ElementFromPoint at minimum, the text-line lookup
	//several more on top), and doing that on every one of up to 500 haptic frames a second - which
	//is what both were doing before this round - is real, avoidable CPU cost. ~33 refreshes/sec is
	//still far more than fast enough to feel continuous for either a cursor-shape classification
	//or a text line's own (slowly-changing-in-practice) on-screen position.
	static constexpr int uiaLookupThrottleFrames = 15;

	int controlTypeLookupCounter = 0;
	bool cachedIsControlType = false;

	int lineLookupCounter = 0;
	bool haveCachedLine = false;
	//Both in workspace units, refreshed only every uiaLookupThrottleFrames frames by
	//computeTextLineDetentForce() - see that method's own comment.
	Vector3 cachedLineCenterWorkspace;
	Vector2 cachedLineHalfExtentWorkspace;   //x = half the line's own on-screen width, y = half its height

	//Per-axis line-detent tuning - deliberately its own small set rather than reusing
	//ButtonObject's or DetentTuning.h's own constants, since this pulls toward a single line
	//rather than normalizing against an arbitrary element's own scale the way theirs do.
	//
	//Reworked this round ("the detents on text lines don't feel super clear right now
	//(especially in the Y axis)") from a single isotropic spring (one fixed-radius deadzone/
	//stiffness pulling toward the line's center equally on both axes) into two independent
	//AXIS springs, each scaled to a FRACTION of that line's own half-width/half-height
	//(cachedLineHalfExtentWorkspace) rather than a fixed guessed-at radius - so the detent's
	//"notch" always spans the line's own real on-screen size, whatever the current font/zoom
	//happens to be, rather than sometimes being too wide (a fixed deadzone bigger than a small
	//line, which is exactly what was making the notch hard to feel) or too narrow (bigger text).
	//Y is deliberately the strong, clearly-felt axis - a small deadzone fraction plus a taller
	//force ceiling, so moving up/down across the boundary between two lines reads as a definite
	//detent - while X stays deliberately gentle (a wide deadzone fraction, a low force ceiling)
	//so it never fights ordinary left-right reading/scanning along a line the way the OLD
	//whole-paragraph pull used to (see isCursorOverTextField()'s own comment on that bug).
	//
	//Force ceilings brought back down this round ("the gain for text line detents has become
	//very high... can we bring the gain/force caps in line with other detent objects?"):
	//lineDetentForceLimitY was 0.006f - the same force ceiling ObjectFactory::getHapticPropsOfType()
	//(ObjectsFactory.cpp) gives UIElementType::Window (the active-slot front wall you physically
	//push through, a deliberately firm interaction), not the Button/ListItem/MenuItem elements
	//this line detent actually replaces/competes with (see suppressButtonDetents at this class's
	//own call site, FFUIDesktop::processForces()) - those cap at 0.0012-0.0015, roughly a
	//quarter of what this was using. Retuned to sit in that same range instead - Y now matches
	//Button/ListItem's own ceiling exactly (the strongest of the three, appropriate for this
	//class's own "clearly-felt" strong axis), and X is scaled down by the same factor as before
	//to preserve the existing "Y strong/X gentle" ratio between the two axes.
	float lineDetentDeadzoneFractionY = 0.15f;  //flat zone = 15% of the line's own half-height
	float lineDetentForceLimitY = 0.0015f;
	float lineDetentDeadzoneFractionX = 0.5f;   //flat zone = 50% of the line's own half-width -
	                                             //only nudges once you're out past the line's own
	                                             //midpoint toward its start/end.
	float lineDetentForceLimitX = 0.0004f;

	//Set by update() every call (even a miss - None if the cursor isn't TextField, or if it is
	//but computeTextLineDetentForce() found nothing) - see isCursorOverTextField()'s own comment
	//for why FFUIDesktop.cpp needs to read this back the same tick.
	CursorHapticState lastClassifiedState = CursorHapticState::None;

	HapticVibration textFieldVibration;
	HapticVibration clickableVibration;
	HapticVibration clickableControlVibration;
	HapticVibration resizeHandleVibration;
	HapticVibration unresponsiveVibration;
};
