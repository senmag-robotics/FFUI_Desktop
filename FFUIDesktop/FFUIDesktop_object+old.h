
#include <iostream>
#include <vector>
#include <memory>
#include <string>

#include "dataTypes.h"
#include "mathTypes.h"



typedef enum {
	objectType_hapticPlane,
	objectType_taskbar,
	objectType_window,
	objectType_desktopItem,
	objectType_button,
	objectType_menu,


}FFUIObject_Types;


typedef struct {
	float stiffness;
	float damping;
	float frictionStatic;
	float frictionDynamic;
}HapticSolidProperties;

typedef struct {
	Vector3		globalPosition;
	Vector3		scale;
	Quaternion	orientation;
	
	char		name[100];

}FFUIObject_Meta;



class FFUIObject {
public:
	FFUIObject(FFUIObject_Meta meta);
	virtual ~FFUIObject() = default;

	Vector3			updateForces(Location cursorLocation);


	 
	virtual Vector3	calculateInteractionForce(Location localPos);


protected:

	FFUIObject_Meta	objectMeta;



private:
	Vector3		getLocalStylusPosition(Location stylusPosition);
	
	std::vector<FFUIObject> children;

};