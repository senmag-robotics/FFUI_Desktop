#include "SetupConfirmationPrompt.h"
#include "FFUIDesktop.h"   //for pAssistantVoice, the assistant's own dedicated SAPI voice
#include <iostream>  //for mirroring narrator requests to the console - see each Speak() call site below

void SetupConfirmationPrompt::start(Location currentLoc, const std::wstring& actionDescription) {
	centerPosition = currentLoc.position;
	previousPosition = centerPosition;
	startTime = std::chrono::steady_clock::now();
	selection = SetupConfirmationSelection::Cancel;
	lastNarratedSelection = SetupConfirmationSelection::Cancel;

	if (pAssistantVoice) {
		std::wstring toSpeak = L"Do you want to " + actionDescription +
			L"? Move up to confirm, move down to cancel, then press the select button. "
			L"Currently: cancel.";
		// Mirrors every narrator request to the console, tagged by source - "mirror all the
		// narrator requests to the console please", per the request.
		std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
		std::cout << "[Narrate:SetupConfirm] \"" << toSpeakNarrow << "\"" << std::endl;
		pAssistantVoice->Speak(toSpeak.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
	}
}

Vector3 SetupConfirmationPrompt::updateForce(Location currentLoc) {
	Vector3 offsetFromCenter = currentLoc.position - centerPosition;

	//Outside the deadzone, the offset decides the selection outright; inside it, keep whatever
	//was last selected - this is what gives each option a "notch" rather than a knife-edge flip
	//exactly at center.
	if (offsetFromCenter.y > SELECTION_DEADZONE) {
		selection = SetupConfirmationSelection::Confirm;
	}
	else if (offsetFromCenter.y < -SELECTION_DEADZONE) {
		selection = SetupConfirmationSelection::Cancel;
	}

	if (selection != lastNarratedSelection) {
		if (pAssistantVoice) {
			const wchar_t* toSpeak = (selection == SetupConfirmationSelection::Confirm) ? L"Confirm" : L"Cancel";
			std::cout << "[Narrate:SetupConfirm] \"" << (selection == SetupConfirmationSelection::Confirm ? "Confirm" : "Cancel") << "\"" << std::endl;
			pAssistantVoice->Speak(toSpeak, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
		}
		lastNarratedSelection = selection;
	}

	float detentDirection = (selection == SetupConfirmationSelection::Confirm) ? 1.0f : -1.0f;
	Vector3 targetDetent = centerPosition + Vector3(0, DETENT_OFFSET * detentDirection, 0);

	//Spring toward the target detent, plus a term damping frame-to-frame position change (a
	//simple discrete velocity-damping approximation - see the constant's own comment) to keep
	//an underdamped spring from ringing/buzzing rather than settling.
	Vector3 springForce = (targetDetent - currentLoc.position) * SPRING_CONSTANT;
	Vector3 velocity = currentLoc.position - previousPosition;
	Vector3 dampingForce = velocity * -DAMPING_CONSTANT;
	previousPosition = currentLoc.position;

	Vector3 combined = springForce + dampingForce;
	if (combined.length() > MAX_FORCE) {
		combined = combined.normalized() * MAX_FORCE;
	}

	//Ease in over RAMP_IN_DURATION rather than snapping to near-full force the instant the
	//prompt starts (the default Cancel selection is already DETENT_OFFSET away on frame one).
	//Both sides cast to milliseconds explicitly before dividing - steady_clock::duration's raw
	//tick unit isn't milliseconds, so comparing .count() values directly without this cast would
	//silently produce a meaningless ratio.
	auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);
	float rampFraction = static_cast<float>(elapsedMs.count()) / static_cast<float>(RAMP_IN_DURATION.count());
	if (rampFraction < 0.0f) rampFraction = 0.0f;  //defensive, shouldn't happen
	if (rampFraction < 1.0f) {
		combined = combined * rampFraction;
	}

	return combined;
}
