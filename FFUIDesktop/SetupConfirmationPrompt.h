#pragma once

#include "mathTypes.h"
#include <chrono>
#include <string>

//Which way the current position is leaning, relative to where the prompt started.
enum class SetupConfirmationSelection {
	Cancel,   //safe default - nothing happens if the user releases without moving
	Confirm
};

//Renders a two-detent "confirm/cancel" haptic choice, used to ask the user whether to enable
//the Claude AI assistant before AssistantSetup is allowed to touch anything (install software,
//open a login window, change Windows settings). Entirely haptic-thread-owned: start() and
//updateForce() are both called every relevant haptic frame from VoiceAssistant's gesture state
//machine (itself driven from FFUIDesktop::updateFrame()) - never from the voice worker thread,
//so no locking is needed here, matching how AuxGestureState's own fields are haptic-thread-only.
//
//Two virtual "detents" (upper = confirm, lower = cancel) sit along the device's Y axis -
//confirmed as the up/down axis by this codebase's own "Workspace Upper/Lower Boundary" naming
//in FFUIDesktop::addBoundaryPlanes() - centered on wherever the stylus is when the prompt
//starts. A small spring pulls the stylus toward whichever detent the current selection favors,
//giving each option a felt "notch" and a clear flip as the user crosses the middle.
class SetupConfirmationPrompt {
public:
	//Called once when the prompt begins. Records the center position, resets the selection to
	//the safe Cancel default, and speaks the full question once: "Do you want to
	//{actionDescription}? Move up to confirm, move down to cancel, then press the select
	//button. Currently: cancel." actionDescription defaults to the original AI-assistant
	//wording, but VoiceAssistant reuses this same prompt for the plain-AUX-hold dictate gesture
	//too (both need the same one-time setup - see AssistantSetup's own comment) with a distinct
	//description ("enable the microphone and voice detection feature") so the two gestures don't
	//read as offering the exact same thing.
	void start(Location currentLoc, const std::wstring& actionDescription = L"enable the Claude AI assistant");

	//Called every haptic frame while the prompt is active. Updates the tracked selection
	//(narrating via pAssistantVoice only on change, same anti-repeat spirit as this codebase's
	//narration elsewhere) and returns the force to send to the device this frame - callers
	//should use this force in place of the normal per-object/boundary force entirely for the
	//frame, not add it to anything else.
	Vector3 updateForce(Location currentLoc);

	//Read once when the front/select button is pressed (see VoiceAssistant::commitSetupConfirmation()),
	//to decide whether to start AssistantSetup.
	bool wasConfirmed() const { return selection == SetupConfirmationSelection::Confirm; }

private:
	Vector3 centerPosition{ 0, 0, 0 };
	Vector3 previousPosition{ 0, 0, 0 };  //last frame's position, for the velocity-damping term below
	std::chrono::steady_clock::time_point startTime{};  //for the force ramp-in, see updateForce()
	SetupConfirmationSelection selection = SetupConfirmationSelection::Cancel;
	SetupConfirmationSelection lastNarratedSelection = SetupConfirmationSelection::Cancel;

	//All constants below are starting points, not verified against real hardware - tune by
	//feel, same category as TAP_MAX/TAP_TO_HOLD_WINDOW in VoiceAssistant.h.
	static constexpr float DETENT_OFFSET = 15.0f;       //device-space units from center to each detent
	static constexpr float SELECTION_DEADZONE = 3.0f;   //hysteresis band around center so selection doesn't flicker at the midpoint
	static constexpr float MAX_FORCE = 0.0025f;          //matches processForces()'s globalUiForceLimit - "similar magnitude to UI elements" per request

	//Chosen so the raw spring reaches MAX_FORCE only right at DETENT_OFFSET itself, tapering
	//smoothly the rest of the way rather than saturating the clamp across most of the travel -
	//a saturated clamp (the previous tuning) removes the natural deceleration a spring should
	//give as you approach the target, which is what was producing the reported instability.
	static constexpr float SPRING_CONSTANT = MAX_FORCE / DETENT_OFFSET;

	//A simple per-frame velocity-damping term (opposes frame-to-frame position change) to kill
	//the oscillation/ringing an undamped spring produces on its own, on top of the detuned
	//stiffness above. Not physically calibrated (no real dt is available here, just "position
	//changed since last frame"), but this is the standard cheap approximation for a discrete-
	//time spring-damper and should meaningfully reduce buzzing even before hardware tuning.
	static constexpr float DAMPING_CONSTANT = 0.00f;

	//Eases the force in linearly over this long after start(), rather than jumping straight to
	//near-full magnitude the instant the prompt begins (which happens today because the default
	//selection - Cancel - is already DETENT_OFFSET away from center on frame one) - a sudden
	//force onset can itself feel like a jolt distinct from genuine spring oscillation.
	static constexpr std::chrono::milliseconds RAMP_IN_DURATION{ 150 };
};
