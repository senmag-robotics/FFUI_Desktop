#pragma once
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <Windows.h>
#include "../WindowScanner.h"

#include "HapticVibration.h"

#include "../dataTypes.h"
#include "../mathTypes.h"



typedef enum {
	objectType_hapticPlane,
	objectType_taskbar,
	objectType_window,
	objectType_desktopItem,
	objectType_button,
	objectType_menu,


}FFUIObject_Types;



typedef struct {
	float solidForceLimit;
	float stiffness;
	float damping;
	float frictionStatic;
	float frictionDynamic;
}HapticSolidProperties;

typedef struct {
	Vector3		globalPosition;
	Vector3		scale;
	Quaternion	orientation;
	HapticSolidProperties	hapticSolidProperties;
	std::string	customName;
	bool snappedToThis;
	UIElementType uiType;

	//Carried over from (the negation of) ScannedUIElement::isEnabled (see WIndowScanner.h) for
	//the Narrate feature (NARRATE_SPEC.md) - lets a disabled third-party button/list item/menu
	//item be spoken as "disabled" rather than silently reading the same as an enabled one.
	//Deliberately named/phrased as "isDisabled" with NO in-class default initializer, rather than
	//"isEnabled = true" - this struct is an unnamed class given a name via typedef (`typedef
	//struct {...} FFUIObject_Meta;`), and MSVC's conformance mode rejects a default member
	//initializer there (error C7626 - "unnamed class used in typedef name cannot declare members
	//other than non-static data members..."), the same restriction snappedToThis just above
	//already avoids by having no initializer of its own. Every other object type (windows,
	//GridTiles, GravityWells), which never sets this explicitly, gets it zero-initialized to
	//false (via each construction site's own `FFUIObject_Meta objectMeta{};`) - i.e. "not known
	//disabled" - matching exactly how snappedToThis's own false default already works.
	bool isDisabled;


}FFUIObject_Meta;


class FFUIObject {
public:
	FFUIObject(FFUIObject_Meta meta);
	virtual ~FFUIObject() = default;

	Vector3			updateForces(Location cursorLocation);
	
	const FFUIObject_Meta& getMeta() const {
		return objectMeta;
	}

	void setSnapped(bool state) {
		 objectMeta.snappedToThis = state;

	}

	virtual Vector3	calculateInteractionForce(Location localLoc);

	Vector3 calculateSnappingForceToThis(Location stylusLocation);

	

protected:

	FFUIObject_Meta	objectMeta;


private:
	Location getLocalStylusLocation(Location stylusPosition);	//uses this objects position & orientation to return the relative position&orientation of the stylus
	Vector3	forceLocalToGlobal(Vector3 force);

	//std::vector<FFUIObject> children;
	std::vector<std::unique_ptr<FFUIObject>> children;

	

};