#include "stdsfx.h"
#include "Gizmo.h"
#include <MathDef.h>
#include <Renderer/RenderCommand.h>

enum GizmoActiveAxis
{
	GZ_ACTIVE_X = 1 << 0,               // Active transformation on the X-axis
	GZ_ACTIVE_Y = 1 << 1,               // Active transformation on the Y-axis
	GZ_ACTIVE_Z = 1 << 2,               // Active transformation on the Z-axis
	GZ_ACTIVE_XYZ = GZ_ACTIVE_X | GZ_ACTIVE_Y | GZ_ACTIVE_Z  // Active transformation on all axes
} ;

/**
 * Active transformation types for the gizmo.
 */
enum GizmoAction
{
	GZ_ACTION_NONE = 0,                 // No active transformation
	GZ_ACTION_TRANSLATE,                // Translation (movement) transformation
	GZ_ACTION_SCALE,                    // Scaling transformation
	GZ_ACTION_ROTATE                    // Rotation transformation
};

/**
 * Indices of the axes.
 * Useful for indexing and iteration.
 */
enum
{
	GZ_AXIS_X = 0,                      // Index of the X-axis
	GZ_AXIS_Y = 1,                      // Index of the Y-axis
	GZ_AXIS_Z = 2,                      // Index of the Z-axis

	GIZMO_AXIS_COUNT = 3                // Total number of axes
};

/**
 * Axis-related data used by gizmos.
 */
struct GizmoAxis
{
	glm::vec3 normal;    // Direction of the axis.
	glm::vec4 color;       // Color used to represent the axis.
};

/**
 * Global variables used by the gizmos.
 * These are shared across all gizmos to maintain consistent behavior and appearance.
 */
struct GizmoGlobals
{
	//viewport size
	int width;
	int height;
	int showXYZText;				   // Whether to show axis labels (X, Y, Z) during transformations.

	GizmoAxis axisCfg[GIZMO_AXIS_COUNT];  // Data related to the 3 axes, globally oriented.

	float gizmoSize;                      // Size of the gizmos. All other metrics are expressed as fractions of this measure.
	float lineWidth;                      // Width of the lines used to draw the gizmos.

	float trArrowWidthFactor;             // Width of the arrows (and cubes) as a fraction of gizmoSize.
	float trArrowLengthFactor;            // Length of the arrows as a fraction of gizmoSize.
	float trPlaneOffsetFactor;            // Offset of the gizmo planes from the center as a fraction of gizmoSize.
	float trPlaneSizeFactor;              // Size of the planes (quad representation) as a fraction of gizmoSize.
	float trCircleRadiusFactor;           // Radius of the central circle as a fraction of gizmoSize.
	Color trCircleColor;                  // Color of the central circle.

	int curAction;                        // Currently active GizmoAction.
	int activeAxis;                       // Active axis (a combination of GizmoActiveAxis flags) for the current action.
	Transform startTransform;             // Backup Transform saved before the transformation begins.
	Transform* activeTransform;           // Pointer to the active Transform to update during transformation.
	glm::vec3 startWorldMouse;              // Position of the mouse in world space at the start of the transformation.
};

/**
 * Temporary data associated with a gizmo.
 * This data is recalculated at each call to DrawGizmo3D(), in immediate-mode style.
 */
struct GizmoData
{
	glm::mat4 invViewProj;                   // Inverted View-Projection glm::mat4.
	Transform* curTransform;              // Pointer to the current Transform. Only one can be the "activeTransform" at a time.
	glm::vec3 axis[GIZMO_AXIS_COUNT];       // Current axes used for transformations (may differ from global axes).
										  // Axes can be in global, view, or local mode depending on configuration.

	float gizmoSize;                      // Actual gizmo size, adjusted to maintain camera-independent scaling.
	glm::vec3 camPos;                       // Position of the camera, extracted during rendering.
	glm::vec3 right, up, forward;           // Local orientation vectors: right, up, and forward.
	int flags;                            // Configuration flags for the gizmo.
};



//---------------------------------------------------------------------------------------------------
// Global Variables Definition
//---------------------------------------------------------------------------------------------------

// Initialize GIZMO with default values
static GizmoGlobals GIZMO = {
	0,0,
	1,
	{
		{{1, 0, 0}, {229/255.f, 72 / 255.f, 91 / 255.f, 255 / 255.f}},
		{{0, 1, 0}, {131 / 255.f, 205 / 255.f, 56 / 255.f, 255 / 255.f}},
		{{0, 0, 1}, {69 / 255.f, 138 / 255.f, 242 / 255.f, 255 / 255.f}}
	},

	1.5f,
	2.5f,

	0.1f,
	0.15f,
	0.3f,
	0.15f,
	0.1f,
	{255, 255, 255, 200},

	GZ_ACTION_NONE,
	0
};


//---------------------------------------------------------------------------------------------------
// Function Declarations - Helper Functions
//---------------------------------------------------------------------------------------------------

/**
 * Compute the axis orientation for a specific gizmo.
 * Determines whether the axes are oriented globally, locally, or in view mode.
 * @param gizmoData Pointer to the data associated with the gizmo.
 */
static void ComputeAxisOrientation(GizmoData* gizmoData);

/**
 * Check if a specific axis is actively used for the current transformation.
 * @param axis The axis to check (GZ_AXIS_X, GZ_AXIS_Y, or GZ_AXIS_Z).
 * @return true if the axis is actively used; false otherwise.
 */
static bool IsGizmoAxisActive(int axis);

/**
 * Check if a gizmo is compatible with a specific transformation type.
 * @param data Pointer to the data associated with the gizmo.
 * @param type A combination of GizmoFlags values specifying the transformation type.
 * @return true if the gizmo supports the requested transformation type; false otherwise.
 */
static bool CheckGizmoType(const GizmoData* data, int type);

/**
 * Check if any gizmo is currently transforming.
 * @return true if a gizmo is active and transforming; false otherwise.
 */
static bool IsGizmoTransforming(void);


/**
 * Check if the current gizmo is the one actively transforming.
 * @param data Pointer to the data associated with the current gizmo.
 * @return true if this gizmo is the one actively transforming; false otherwise.
 */
static bool IsThisGizmoTransforming(const GizmoData* data);

/**
 * Check if the active transformation is of scaling type.
 * @return true if a gizmo is actively scaling; false otherwise.
 */
static bool IsGizmoScaling(void);

/**
 * Check if the active transformation is of translation type.
 * @return true if a gizmo is actively translating; false otherwise.
 */
static bool IsGizmoTranslating(void);

/**
 * Check if the active transformation is of rotation type.
 * @return true if a gizmo is actively rotating; false otherwise.
 */
static bool IsGizmoRotating(void);

/**
 * Convert a screen-space vector to world space.
 * @param source The screen-space glm::vec3 to convert.
 * @param matViewProjInv Pointer to the inverted view-projection glm::mat4.
 * @return The input vector transformed to world space.
 * @note This function does not depend on a Camera. Refer to raylib's glm::vec3Unproject() for the original implementation.
 */
static glm::vec3 Vec3ScreenToWorld(glm::vec3 source, const glm::mat4* matViewProjInv);

/**
 * Generate a world-space ray from a screen-space position.
 * @param position The screen-space position (Vector2) from which to emit the ray.
 * @param matViewProjInv Pointer to the inverted view-projection glm::mat4.
 * @return A Ray representing the world-space ray emitted from the screen position.
 * @note This function does not depend on a Camera. Refer to raylib's GetScreenToWorldRayEx() for the original implementation.
 */
static Ray Vec3ScreenToWorldRay(glm::vec2 position, const glm::mat4* matViewProjInv, float width, float height);


//---------------------------------------------------------------------------------------------------
// Functions Declaration - Drawing functions
//---------------------------------------------------------------------------------------------------

/**
 * Helper function used to draw a scaling gizmo cube on one of the axis
 * @param data data associated with the current gizmo
 * @param axis GZ_AXIS_X, GZ_AXIS_Y or GZ_AXIS_Z
 */
static void DrawGizmoCube(const GizmoData* data, int axis);

/**
 * Helper function used to draw a plane representing two axis together
 * @param data data associated with the current gizmo
 * @param index of the third (locked) axis: GZ_AXIS_X, GZ_AXIS_Y or GZ_AXIS_Z
 */
static void DrawGizmoPlane(const GizmoData* data, int index);

/**
 * Helper function used to draw a translating gizmo arrow on one of the axis
 * @param data data associated with the current gizmo
 * @param axis GZ_AXIS_X, GZ_AXIS_Y or GZ_AXIS_Z
 */
static void DrawGizmoArrow(const GizmoData* data, int axis);

/**
 * Helper function used to draw the gizmo center as a circle always pointing to the camera
 * @param data data associated with the current gizmo
 */
static void DrawGizmoCenter(const GizmoData* data);

/**
 * Helper function used to draw a rotating gizmo circle on one of the axis planes.
 * @param data data associated with the current gizmo
 * @param axis GZ_AXIS_X, GZ_AXIS_Y or GZ_AXIS_Z
 */
static void DrawGizmoCircle(const GizmoData* data, int axis);


//---------------------------------------------------------------------------------------------------
// Function Declarations - Mouse Ray to Gizmo Intersections
//---------------------------------------------------------------------------------------------------

/**
 * Check for intersection between a ray and an Oriented Bounding Box (OBB).
 * @param data Pointer to the data associated with the current gizmo.
 * @param ray Ray emitted from the camera.
 * @param obbCenter Center of the oriented bounding box.
 * @param obbHalfSize Half-size of the oriented bounding box along each axis.
 * @return true if the ray intersects the oriented bounding box; false otherwise.
 */
static bool CheckOrientedBoundingBox(const GizmoData* data, Ray ray, glm::vec3 obbCenter, glm::vec3 obbHalfSize);

/**
 * Check for intersection between a ray and one of the axes of the gizmo.
 * @param data Pointer to the data associated with the current gizmo.
 * @param axis The axis to check (GZ_AXIS_X, GZ_AXIS_Y, or GZ_AXIS_Z).
 * @param ray Ray emitted from the camera.
 * @param type Type of the axis (GIZMO_TRANSLATE or GIZMO_SCALE), needed to differentiate computation when both are present.
 * @return true if the ray intersects the gizmo axis; false otherwise.
 */
static bool CheckGizmoAxis(const GizmoData* data, int axis, Ray ray, int type);

/**
 * Check for intersection between a ray and one of the gizmo planes.
 * Planes are represented by small quads and are associated with translating or scaling along two axes.
 * @param data Pointer to the data associated with the current gizmo.
 * @param axis The axis associated with the plane (GZ_AXIS_X, GZ_AXIS_Y, or GZ_AXIS_Z).
 * @param ray Ray emitted from the camera.
 * @return true if the ray intersects the gizmo plane; false otherwise.
 */
static bool CheckGizmoPlane(const GizmoData* data, int axis, Ray ray);

/**
 * Check for intersection between a ray and one of the gizmo's rotation circles.
 * @param data Pointer to the data associated with the current gizmo.
 * @param index The axis index (GZ_AXIS_X, GZ_AXIS_Y, or GZ_AXIS_Z) corresponding to the rotation circle.
 * @param ray Ray emitted from the camera.
 * @return true if the ray intersects the rotation circle; false otherwise.
 */
static bool CheckGizmoCircle(const GizmoData* data, int index, Ray ray);

/**
 * Check for intersection between a ray and the gizmo center.
 * The gizmo center is represented as a circle always facing the viewer and treated as a sphere.
 * @param data Pointer to the data associated with the current gizmo.
 * @param ray Ray emitted from the camera.
 * @return true if the ray intersects the gizmo center; false otherwise.
 */
static bool CheckGizmoCenter(const GizmoData* data, Ray ray);


//---------------------------------------------------------------------------------------------------
// Function Declarations - Input Handling
//---------------------------------------------------------------------------------------------------

/**
 * Convert the mouse position to a world-space vector.
 * The viewer's distance to the gizmo is used to determine the depth at which the world-space position is computed.
 * @param data Pointer to the data of the current gizmo.
 * @return A glm::vec3 representing the mouse position in world space.
 */
static glm::vec3 GetWorldMouse(const GizmoData* data, glm::vec2 mousepos);


/**
 * Handle all input interactions for the gizmo.
 * Processes input such as mouse and keyboard events to update gizmo states and transformations.
 * @param data Pointer to the data of the current gizmo.
 * @note This function may be modified in future iterations.
 */
static void GizmoHandleInput(const GizmoData* data, bool leftdown, glm::vec2 mousepos);




void SetGizmoViewportSize(int width, int height)
{
	GIZMO.width = width;
	GIZMO.height = height;
};

bool IsGizmoActivate()
{
	return GIZMO.curAction != GZ_ACTION_NONE;
}
//---------------------------------------------------------------------------------------------------
// Functions Definitions - GIZMO API
//---------------------------------------------------------------------------------------------------

bool DrawGizmo3D(const glm::mat4& camView,const glm::mat4& camProj,
	bool leftdown, glm::vec2 mousepos
	,int flags, Transform* transform)
{
	//------------------------------------------------------------------------

	if (flags == GIZMO_DISABLED) return false;
	if (GIZMO.width == 0) {
		printf("set viewport size first: SetViewportSize");
		return false;
	}
	//------------------------------------------------------------------------

	GizmoData data;

	//------------------------------------------------------------------------

	const glm::mat4 matProj = camProj;
	const glm::mat4 matView = camView;
	const glm::mat4 invMat = glm::inverse(matView);
	
	const float* invMatPtr = glm::value_ptr(invMat);
	const float* matViewPtr = glm::value_ptr(matView);

	data.invViewProj = glm::inverse(matProj * matView);

	data.camPos = { invMatPtr[12], invMatPtr[13], invMatPtr[14] };

	data.right = { matViewPtr[0], matViewPtr[4], matViewPtr[8]};
	data.up = { matViewPtr[1], matViewPtr[5], matViewPtr[9]};
	data.forward = glm::normalize(transform->translation-data.camPos);

	data.curTransform = transform;

	data.gizmoSize = GIZMO.gizmoSize * glm::distance(data.camPos, transform->translation) * 0.1f;

	data.flags = flags;

	ComputeAxisOrientation(&data);

	//------------------------------------------------------------------------

	//rlDrawRenderBatchActive();
	//const float prevLineWidth = rlGetLineWidth();
	//rlSetLineWidth(GIZMO.lineWidth);
	//rlDisableBackfaceCulling();
	//rlDisableDepthTest();
	//rlDisableDepthMask();

	//------------------------------------------------------------------------

	for (int i = 0; i < GIZMO_AXIS_COUNT; ++i)
	{
		if (data.flags & GIZMO_TRANSLATE)
		{
			DrawGizmoArrow(&data, i);
		}
		if (data.flags & GIZMO_SCALE)
		{
			DrawGizmoCube(&data, i);
		}
		if ((data.flags & (GIZMO_SCALE | GIZMO_TRANSLATE)) != 0)
		{
			DrawGizmoPlane(&data, i);
		}
		if (data.flags & GIZMO_ROTATE)
		{
			DrawGizmoCircle(&data, i);
		}		
	}
	if ((data.flags & (GIZMO_SCALE | GIZMO_TRANSLATE)) != 0) 
	{
		DrawGizmoCenter(&data);
	}

	//------------------------------------------------------------------------

	//rlDrawRenderBatchActive();
	//rlSetLineWidth(prevLineWidth);
	//rlEnableBackfaceCulling();
	//rlEnableDepthTest();
	//rlEnableDepthMask();

	//------------------------------------------------------------------------

	// If there's an active transformation, only the interested gizmo handles the input
	if (!IsGizmoTransforming() || data.curTransform == GIZMO.activeTransform)
	{
		GizmoHandleInput(&data,leftdown,mousepos);
	}

	//------------------------------------------------------------------------

	return IsThisGizmoTransforming(&data);
}

void SetGizmoSize(float size)
{
	GIZMO.gizmoSize = fmaxf(0, size);
}

void SetGizmoLineWidth(float width)
{
	GIZMO.lineWidth = fmaxf(0, width);
}

void SetGizmoColors(Color x, Color y, Color z, Color center)
{
	GIZMO.axisCfg[GZ_AXIS_X].color = x;
	GIZMO.axisCfg[GZ_AXIS_Y].color = y;
	GIZMO.axisCfg[GZ_AXIS_Z].color = z;
	GIZMO.trCircleColor = center;
}

void SetGizmoGlobalAxis(glm::vec3 right, glm::vec3 up, glm::vec3 forward)
{
	GIZMO.axisCfg[GZ_AXIS_X].normal = glm::normalize(right);
	GIZMO.axisCfg[GZ_AXIS_Y].normal = glm::normalize(up);
	GIZMO.axisCfg[GZ_AXIS_Z].normal = glm::normalize(forward);
}

Transform GizmoIdentity(void)
{
	return {
		glm::vec3{0,0,0},
		glm::quat{0,0,0,1},
		glm::vec3{1,1,1}
	};
}

glm::mat4 GizmoToMatrix(Transform transform)
{
	return transform.GetMatrix();
}


//---------------------------------------------------------------------------------------------------
// Functions Definitions - Helper functions
//---------------------------------------------------------------------------------------------------

static void ComputeAxisOrientation(GizmoData* gizmoData)
{
	int flags = gizmoData->flags;

	// Scaling is currently supported only in local mode
	if (flags & GIZMO_SCALE)
	{
		flags &= ~GIZMO_VIEW;
		flags |= GIZMO_LOCAL;
	}

	if (flags & GIZMO_VIEW)
	{
		gizmoData->axis[GZ_AXIS_X] = gizmoData->right;
		gizmoData->axis[GZ_AXIS_Y] = gizmoData->up;
		gizmoData->axis[GZ_AXIS_Z] = gizmoData->forward;
	}
	else
	{
		gizmoData->axis[GZ_AXIS_X] = GIZMO.axisCfg[GZ_AXIS_X].normal;
		gizmoData->axis[GZ_AXIS_Y] = GIZMO.axisCfg[GZ_AXIS_Y].normal;
		gizmoData->axis[GZ_AXIS_Z] = GIZMO.axisCfg[GZ_AXIS_Z].normal;

		if (flags & GIZMO_LOCAL)
		{
			for (int i = 0; i < 3; ++i)
			{
				gizmoData->axis[i] = glm::normalize(
					glm::rotate(gizmoData->curTransform->rotation, gizmoData->axis[i]));
			}
		}
	}
}

static bool IsGizmoAxisActive(int axis)
{
	return (axis == GZ_AXIS_X && (GIZMO.activeAxis & GZ_ACTIVE_X)) ||
		(axis == GZ_AXIS_Y && (GIZMO.activeAxis & GZ_ACTIVE_Y)) ||
		(axis == GZ_AXIS_Z && (GIZMO.activeAxis & GZ_ACTIVE_Z));
}

static bool CheckGizmoType(const GizmoData* data, int type)
{
	return (data->flags & type) == type;
}


static bool IsGizmoTransforming(void)
{
	return GIZMO.curAction != GZ_ACTION_NONE;
}

static bool IsThisGizmoTransforming(const GizmoData* data)
{
	return IsGizmoTransforming() && data->curTransform == GIZMO.activeTransform;
}

static bool IsGizmoScaling(void)
{
	return GIZMO.curAction == GZ_ACTION_SCALE;
}

static bool IsGizmoTranslating(void)
{
	return GIZMO.curAction == GZ_ACTION_TRANSLATE;
}

static bool IsGizmoRotating(void)
{
	return GIZMO.curAction == GZ_ACTION_ROTATE;
}

glm::quat QuaternionTransform(const glm::quat& q, const glm::mat4& mat)
{
	glm::vec4 v(q.x, q.y, q.z, q.w);
	glm::vec4 r = mat * v;
	return glm::quat(r.w, r.x, r.y, r.z);
}

static glm::vec3 Vec3ScreenToWorld(glm::vec3 source, const glm::mat4* matViewProjInv)
{
	glm::quat rot = { 1.0f , source.x, source.y, source.z};
	
	const glm::quat qt = QuaternionTransform(rot, *matViewProjInv);// glm::normalize(glm::quat_cast(*matViewProjInv) * rot); //rot *matViewProjInv);
	return {
		qt.x / qt.w,
		qt.y / qt.w,
		qt.z / qt.w
	};
}

static Ray Vec3ScreenToWorldRay(glm::vec2 position, const glm::mat4* matViewProjInv,float width,float height)
{
	Ray ray;

	const glm::vec2 deviceCoords = {(2.0f * position.x) / width - 1.0f, 1.0f - (2.0f * position.y) / height};

	const glm::vec3 nearPoint = Vec3ScreenToWorld({deviceCoords.x, deviceCoords.y, 0.0f}, matViewProjInv);

	const glm::vec3 farPoint = Vec3ScreenToWorld({deviceCoords.x, deviceCoords.y, 1.0f}, matViewProjInv);

	const glm::vec3 cameraPlanePointerPos = Vec3ScreenToWorld({deviceCoords.x, deviceCoords.y, -1.0f},
	                                                        matViewProjInv);

	const glm::vec3 direction = glm::normalize(farPoint - nearPoint);

	/*
	if (camera.projection == CAMERA_PERSPECTIVE) ray.position = camera.position;
	else if (camera.projection == CAMERA_ORTHOGRAPHIC) ray.position = cameraPlanePointerPos;
	*/
	ray.position = cameraPlanePointerPos;

	// Apply calculated vectors to ray
	ray.direction = direction;

	return ray;
}


//---------------------------------------------------------------------------------------------------
// Functions Definition - Drawing functions
//---------------------------------------------------------------------------------------------------

static void DrawGizmoCube(const GizmoData* data, int axis)
{
	if (IsThisGizmoTransforming(data) && (!IsGizmoAxisActive(axis) || !IsGizmoScaling()))
	{
		return;
	}

	const float gizmoSize = CheckGizmoType(data, GIZMO_SCALE | GIZMO_TRANSLATE)
		                        ? data->gizmoSize * 0.5f
		                        : data->gizmoSize;

	const glm::vec3 endPos = data->curTransform->translation + 
	                                  data->axis[axis] * gizmoSize * (1.0f - GIZMO.trArrowWidthFactor);

	RenderCommand::FlushLine(data->curTransform->translation, endPos, GIZMO.axisCfg[axis].color);

	const float boxSize = data->gizmoSize * GIZMO.trArrowWidthFactor;

	const glm::vec3 dim1 = data->axis[(axis + 1) % 3] * boxSize;
	const glm::vec3 dim2 = data->axis[(axis + 2) % 3] * boxSize;
	const glm::vec3 n = data->axis[axis];
	const Color col = GIZMO.axisCfg[axis].color;

	const glm::vec3 depth = (n* boxSize);

	const glm::vec3 a = endPos - (dim1 * 0.5f)- (dim2 *0.5f);
	const glm::vec3 b = (a + dim1);
	const glm::vec3 c = (b + dim2);
	const glm::vec3 d = (a + dim2);

	const glm::vec3 e = (a + depth);
	const glm::vec3 f = (b + depth);
	const glm::vec3 g = (c + depth);
	const glm::vec3 h = (d + depth);

	RenderCommand::FlushQuad(a, b, c, d, col);
	RenderCommand::FlushQuad(e, f, g, h, col);
	RenderCommand::FlushQuad(a, e, f, d, col);
	RenderCommand::FlushQuad(b, f, g, c, col);
	RenderCommand::FlushQuad(a, b, f, e, col);
	RenderCommand::FlushQuad(c, g, h, d, col);
}


//---------------------------------------------------------------------------------------------------------

static void DrawGizmoPlane(const GizmoData* data, int index)
{
	if (IsThisGizmoTransforming(data))
	{
		return;
	}

	const glm::vec3 dir1 = data->axis[(index + 1) % 3];
	const glm::vec3 dir2 = data->axis[(index + 2) % 3];
	const Color col = GIZMO.axisCfg[index].color;

	const float offset = GIZMO.trPlaneOffsetFactor * data->gizmoSize;
	const float size = GIZMO.trPlaneSizeFactor * data->gizmoSize;

	const glm::vec3 a = ((data->curTransform->translation+ (dir1* offset))+ (dir2 *offset));
	const glm::vec3 b = (a +(dir1 * size));
	const glm::vec3 c = (b +(dir2 * size));
	const glm::vec3 d = (a +(dir2 * size));


	glm::vec4 color = { col.r, col.g, col.b, ((float)col.a * 0.5f) };
	RenderCommand::FlushQuad(a, b, c, d, color);
	RenderCommand::FlushLine(a, b, col);
	RenderCommand::FlushLine(b, c, col);
	RenderCommand::FlushLine(c, d, col);
	RenderCommand::FlushLine(d, a, col);

}

static void DrawGizmoArrow(const GizmoData* data, int axis)
{
	if (IsThisGizmoTransforming(data) && (!IsGizmoAxisActive(axis) || !IsGizmoTranslating()))
	{
		return;
	}

	const glm::vec3 endPos = (data->curTransform->translation+
	                                  (data->axis[axis]*
	                                               data->gizmoSize * (1.0f - GIZMO.trArrowLengthFactor)));

	if (!(data->flags & GIZMO_SCALE))
	{
		RenderCommand::FlushLine(data->curTransform->translation, endPos, GIZMO.axisCfg[axis].color);
	}
	const float arrowLength = data->gizmoSize * GIZMO.trArrowLengthFactor;
	const float arrowWidth = data->gizmoSize * GIZMO.trArrowWidthFactor;

	const glm::vec3 dim1 = (data->axis[(axis + 1) % 3] * arrowWidth);
	const glm::vec3 dim2 = (data->axis[(axis + 2) % 3] * arrowWidth);
	const glm::vec3 n = data->axis[axis];
	const Color col = GIZMO.axisCfg[axis].color;

	const glm::vec3 v = (endPos + (n * arrowLength));

	const glm::vec3 a = ((endPos -(dim1 * 0.5f)) - (dim2 * 0.5f));
	const glm::vec3 b = (a + dim1);
	const glm::vec3 c = (b + dim2);
	const glm::vec3 d = (a + dim2);

	RenderCommand::FlushTriangle(a, b, c, col);
	RenderCommand::FlushTriangle(a, c, d, col);
	RenderCommand::FlushTriangle(a, v, b, col);
	RenderCommand::FlushTriangle(b, v, c, col);
	RenderCommand::FlushTriangle(c, v, d, col);
	RenderCommand::FlushTriangle(d, v, a, col);
}
static void DrawGizmoCenter(const GizmoData* data)
{
	const glm::vec3 origin = data->curTransform->translation;

	const float radius = data->gizmoSize * GIZMO.trCircleRadiusFactor;
	const Color col = GIZMO.trCircleColor;
	const int angleStep = 15;


	for (int i = 0; i < 360; i += angleStep)
	{
		float angle = glm::radians( (float)i);
		glm::vec3 p = (data->right * sinf(angle) * radius);
		p = (p + (data->up * cosf(angle) * radius)) + origin;

		angle += glm::radians((float)angleStep);
		glm::vec3 p2 = (data->right* sinf(angle) * radius);
		p2 = (p2+ (data->up *cosf(angle) * radius)) + origin;
		
		RenderCommand::FlushLine(p, p2, col);
	}

	if (GIZMO.showXYZText)
	{
		//x
		glm::vec3 originX = (data->curTransform->translation +
			(data->axis[0] * data->gizmoSize * (1.0f - GIZMO.trArrowLengthFactor + 0.25f)));
		const float fontscale = 0.05f * data->gizmoSize * (1.0f - GIZMO.trArrowLengthFactor + 0.2f);
		RenderCommand::FlushLine(originX - fontscale * data->right - fontscale * data->up, originX + fontscale * data->right + fontscale * data->up, glm::vec4(1, 0, 0, 1));
		RenderCommand::FlushLine(originX + fontscale * data->right - fontscale * data->up, originX - fontscale * data->right + fontscale * data->up, glm::vec4(1, 0, 0, 1));
		//y
		glm::vec3 originY = (data->curTransform->translation +
			(data->axis[1] * data->gizmoSize * (1.0f - GIZMO.trArrowLengthFactor + 0.25f)));
		RenderCommand::FlushLine(originY - fontscale * data->right + fontscale * data->up, originY, glm::vec4(0, 1, 0, 1));
		RenderCommand::FlushLine(originY, originY + fontscale * data->right + fontscale * data->up, glm::vec4(0, 1, 0, 1));
		RenderCommand::FlushLine(originY, originY - fontscale * data->up, glm::vec4(0, 1, 0, 1));

		//z
		glm::vec3 originZ = (data->curTransform->translation +
			(data->axis[2] * data->gizmoSize * (1.0f - GIZMO.trArrowLengthFactor + 0.25f)));
		RenderCommand::FlushLine(originZ - fontscale * data->right - fontscale * data->up, originZ + fontscale * data->right - fontscale * data->up, glm::vec4(0, 0, 1, 1));
		RenderCommand::FlushLine(originZ - fontscale * data->right + fontscale * data->up, originZ + fontscale * data->right + fontscale * data->up, glm::vec4(0, 0, 1, 1));
		RenderCommand::FlushLine(originZ - fontscale * data->right - fontscale * data->up, originZ + fontscale * data->right + fontscale * data->up, glm::vec4(0, 0, 1, 1));
	}

}

static void DrawGizmoCircle(const GizmoData* data, int axis)
{
	if (IsThisGizmoTransforming(data) && (!IsGizmoAxisActive(axis) || !IsGizmoRotating()))
	{
		return;
	}
	
	const glm::vec3 origin = data->curTransform->translation;

	const glm::vec3 dir1 = data->axis[(axis + 1) % 3];
	const glm::vec3 dir2 = data->axis[(axis + 2) % 3];

	const Color col = GIZMO.axisCfg[axis].color;

	const float radius = data->gizmoSize;
	const int angleStep = 10;

	for (int i = 0; i < 360; i += angleStep)
	{
		float angle = glm::radians((float)i);
		glm::vec3 p =(dir1* sinf(angle) * radius);
		p = (p + (dir2* cosf(angle) * radius)) + origin;

		angle += glm::radians((float)angleStep); 
		glm::vec3 p2 = (dir1 * sinf(angle) * radius);
		p2 = (p2 + (dir2* cosf(angle) * radius)) + origin;

		RenderCommand::FlushLine(p, p2, col);
	}

}


//---------------------------------------------------------------------------------------------------
// Functions Definition - Mouse ray to Gizmo intersections
//---------------------------------------------------------------------------------------------------

static bool CheckOrientedBoundingBox(const GizmoData* data, Ray ray, glm::vec3 obbCenter, glm::vec3 obbHalfSize)
{
	const glm::vec3 oLocal = (ray.position - obbCenter);

	Ray localRay;

	localRay.position.x = glm::dot(oLocal, data->axis[GZ_AXIS_X]);
	localRay.position.y = glm::dot(oLocal, data->axis[GZ_AXIS_Y]);
	localRay.position.z = glm::dot(oLocal, data->axis[GZ_AXIS_Z]);

	localRay.direction.x = glm::dot(ray.direction, data->axis[GZ_AXIS_X]);
	localRay.direction.y = glm::dot(ray.direction, data->axis[GZ_AXIS_Y]);
	localRay.direction.z = glm::dot(ray.direction, data->axis[GZ_AXIS_Z]);

	const glm::AABB aabbLocal ( {-obbHalfSize.x, -obbHalfSize.y, -obbHalfSize.z},
		{+obbHalfSize.x, +obbHalfSize.y, +obbHalfSize.z} );

	return GetRayCollisionBox(localRay, aabbLocal).hit;
}

static bool CheckGizmoAxis(const GizmoData* data, int axis, Ray ray, int type)
{
	float halfDim[3];

	halfDim[axis] = data->gizmoSize * 0.5f;
	halfDim[(axis + 1) % 3] = data->gizmoSize * GIZMO.trArrowWidthFactor * 0.5f;
	halfDim[(axis + 2) % 3] = halfDim[(axis + 1) % 3];

	if (type == GIZMO_SCALE && CheckGizmoType(data, GIZMO_TRANSLATE | GIZMO_SCALE))
	{
		halfDim[axis] *= 0.5f;
	}

	const glm::vec3 obbCenter = (data->curTransform->translation + (data->axis[axis] * halfDim[axis]));

	return CheckOrientedBoundingBox(data, ray, obbCenter, glm::vec3{halfDim[0], halfDim[1], halfDim[2]});
}

static bool CheckGizmoPlane(const GizmoData* data, int axis, Ray ray)
{
	const glm::vec3 dir1 = data->axis[(axis + 1) % 3];
	const glm::vec3 dir2 = data->axis[(axis + 2) % 3];


	const float offset = GIZMO.trPlaneOffsetFactor * data->gizmoSize;
	const float size = GIZMO.trPlaneSizeFactor * data->gizmoSize;

	const glm::vec3 a = ((data->curTransform->translation+ (dir1* offset))+
	                             (dir2* offset));
	const glm::vec3 b = (a +(dir1 *  size));
	const glm::vec3 c = (b +(dir2 *  size));
	const glm::vec3 d = (a +(dir2 *  size));

	return GetRayCollisionQuad(ray, a, b, c, d).hit;
}

static bool CheckGizmoCircle(const GizmoData* data, int index, Ray ray)
{
	const glm::vec3 origin = data->curTransform->translation;

	const glm::vec3 dir1 = data->axis[(index + 1) % 3];
	const glm::vec3 dir2 = data->axis[(index + 2) % 3];

	const float circleRadius = data->gizmoSize;
	const int angleStep = 10;

	const float sphereRadius = /*2.0f **/ circleRadius * sinf(glm::radians((float)angleStep/ 2.0f));

	for (int i = 0; i < 360; i += angleStep)
	{
		const float angle = glm::radians((float)i);
		glm::vec3 p = (origin + (dir1 * sinf(angle) * circleRadius));
		p = (p+ (dir2 * cosf(angle) * circleRadius));

		if (GetRayCollisionSphere(ray, p, sphereRadius).hit)
		{
			return true;
		}
	}

	return false;
}

static bool CheckGizmoCenter(const GizmoData* data, Ray ray)
{
	return GetRayCollisionSphere(ray, data->curTransform->translation, data->gizmoSize * GIZMO.trCircleRadiusFactor).
		hit;
}


//---------------------------------------------------------------------------------------------------
// Functions Definitions - Input Handling
//---------------------------------------------------------------------------------------------------

static glm::vec3 GetWorldMouse(const GizmoData* data,glm::vec2 mousepos)
{
	const float dist = glm::distance(data->camPos, data->curTransform->translation);
	const Ray mouseRay = Vec3ScreenToWorldRay(mousepos, &data->invViewProj,GIZMO.width, GIZMO.height);
	return (mouseRay.position + (mouseRay.direction * dist));
}

static void GizmoHandleInput(const GizmoData* data,bool leftdown,glm::vec2 mousepos)
{
	int action = GIZMO.curAction;

	if (action != GZ_ACTION_NONE)
	{
		if (!leftdown)
		{
			//SetMouseCursor(MOUSE_CURSOR_DEFAULT);
			action = GZ_ACTION_NONE;
			GIZMO.activeAxis = 0;
		}
		else
		{
			const glm::vec3 endWorldMouse = GetWorldMouse(data, mousepos);
			const glm::vec3 pVec = (endWorldMouse - GIZMO.startWorldMouse);

			switch (action)
			{
			case GZ_ACTION_TRANSLATE:
				{
					GIZMO.activeTransform->translation = GIZMO.startTransform.translation;
					if (GIZMO.activeAxis == GZ_ACTIVE_XYZ)
					{
						GIZMO.activeTransform->translation = (GIZMO.activeTransform->translation + Vector3Project(pVec, data->right));
						GIZMO.activeTransform->translation = (GIZMO.activeTransform->translation + Vector3Project(pVec, data->up));
					}
					else
					{
						if (GIZMO.activeAxis & GZ_ACTIVE_X)
						{
							const glm::vec3 prj = Vector3Project(pVec, data->axis[GZ_AXIS_X]);
							GIZMO.activeTransform->translation = (GIZMO.activeTransform->translation+prj);
						}
						if (GIZMO.activeAxis & GZ_ACTIVE_Y)
						{
							const glm::vec3 prj = Vector3Project(pVec, data->axis[GZ_AXIS_Y]);
							GIZMO.activeTransform->translation = (GIZMO.activeTransform->translation+ prj);
						}
						if (GIZMO.activeAxis & GZ_ACTIVE_Z)
						{
							const glm::vec3 prj = Vector3Project(pVec, data->axis[GZ_AXIS_Z]);
							GIZMO.activeTransform->translation = (GIZMO.activeTransform->translation+ prj);
						}
					}
				}
				break;
			case GZ_ACTION_SCALE:
				{
					GIZMO.activeTransform->scale = GIZMO.startTransform.scale;
					if (GIZMO.activeAxis == GZ_ACTIVE_XYZ)
					{
						const float delta = glm::dot(pVec, GIZMO.axisCfg[GZ_AXIS_X].normal);
						GIZMO.activeTransform->scale = (GIZMO.activeTransform->scale+ delta);
					}
					else
					{
						if (GIZMO.activeAxis & GZ_ACTIVE_X)
						{
							const glm::vec3 prj = Vector3Project(pVec, GIZMO.axisCfg[GZ_AXIS_X].normal);
							// data->axis[GIZMO_AXIS_X]);
							GIZMO.activeTransform->scale = (GIZMO.activeTransform->scale+ prj);
						}
						if (GIZMO.activeAxis & GZ_ACTIVE_Y)
						{
							const glm::vec3 prj = Vector3Project(pVec, GIZMO.axisCfg[GZ_AXIS_Y].normal);
							GIZMO.activeTransform->scale = (GIZMO.activeTransform->scale+ prj);
						}
						if (GIZMO.activeAxis & GZ_ACTIVE_Z)
						{
							const glm::vec3 prj = Vector3Project(pVec, GIZMO.axisCfg[GZ_AXIS_Z].normal);
							GIZMO.activeTransform->scale = (GIZMO.activeTransform->scale+ prj);
						}
					}
				}
				break;
			case GZ_ACTION_ROTATE:
				{
					GIZMO.activeTransform->rotation = GIZMO.startTransform.rotation;
					//SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
					const float delta = glm::clamp(glm::dot(pVec, (data->right +  data->up)), -glm::pi<float>()*2.f,
					                          +glm::pi<float>() * 2.f);
					if (GIZMO.activeAxis & GZ_ACTIVE_X)
					{
						const glm::quat q = glm::angleAxis(delta, data->axis[GZ_AXIS_X]);// QuaternionFromAxisAngle(data->axis[GZ_AXIS_X], delta);
						GIZMO.activeTransform->rotation = q * GIZMO.activeTransform->rotation;// QuaternionMultiply(q, GIZMO.activeTransform->rotation);
					}
					if (GIZMO.activeAxis & GZ_ACTIVE_Y)
					{
						const glm::quat q = glm::angleAxis(delta, data->axis[GZ_AXIS_Y]); //QuaternionFromAxisAngle(data->axis[GZ_AXIS_Y], delta);
						GIZMO.activeTransform->rotation = q * GIZMO.activeTransform->rotation;// QuaternionMultiply(q, GIZMO.activeTransform->rotation);
					}
					if (GIZMO.activeAxis & GZ_ACTIVE_Z)
					{
						const glm::quat q = glm::angleAxis(delta, data->axis[GZ_AXIS_Z]);
						GIZMO.activeTransform->rotation = q * GIZMO.activeTransform->rotation;// QuaternionMultiply(q, GIZMO.activeTransform->rotation);
					}
					//BUG FIXED: Updating the transform "starting point" prevents uncontrolled rotations in local mode
					GIZMO.startTransform = *GIZMO.activeTransform;
					GIZMO.startWorldMouse = endWorldMouse;
				}
				break;
			default:
				break;
			}
		}
	}
	else
	{
		if (leftdown)
		{
			const Ray mouseRay = Vec3ScreenToWorldRay(mousepos, &data->invViewProj,GIZMO.width,GIZMO.height);

			int hit = -1;
			action = GZ_ACTION_NONE;

			for (int k = 0; hit == -1 && k < 2; ++k)
			{
				const int gizmoFlag = k == 0 ? GIZMO_SCALE : GIZMO_TRANSLATE;
				const int gizmoAction = k == 0 ? GZ_ACTION_SCALE : GZ_ACTION_TRANSLATE;

				if (data->flags & gizmoFlag)
				{
					if (CheckGizmoCenter(data, mouseRay))
					{
						action = gizmoAction;
						hit = 6;
						break;
					}
					for (int i = 0; i < GIZMO_AXIS_COUNT; ++i)
					{
						if (CheckGizmoAxis(data, i, mouseRay, gizmoFlag))
						{
							action = gizmoAction;
							hit = i;
							break;
						}
						if (CheckGizmoPlane(data, i, mouseRay))
						{
							action = CheckGizmoType(data, GIZMO_SCALE | GIZMO_TRANSLATE)
								         ? GIZMO_TRANSLATE
								         : gizmoAction;
							hit = 3 + i;
							break;
						}
					}
				}
			}

			if (hit == -1 && data->flags & GIZMO_ROTATE)
			{
				for (int i = 0; i < GIZMO_AXIS_COUNT; ++i)
				{
					if (CheckGizmoCircle(data, i, mouseRay))
					{
						action = GZ_ACTION_ROTATE;
						hit = i;
						break;
					}
				}
			}

			GIZMO.activeAxis = 0;
			if (hit >= 0)
			{
				switch (hit)
				{
				case 0:
					GIZMO.activeAxis = GZ_ACTIVE_X;
					break;
				case 1:
					GIZMO.activeAxis = GZ_ACTIVE_Y;
					break;
				case 2:
					GIZMO.activeAxis = GZ_ACTIVE_Z;
					break;
				case 3:
					GIZMO.activeAxis = GZ_ACTIVE_Y | GZ_ACTIVE_Z;
					break;
				case 4:
					GIZMO.activeAxis = GZ_ACTIVE_X | GZ_ACTIVE_Z;
					break;
				case 5:
					GIZMO.activeAxis = GZ_ACTIVE_X | GZ_ACTIVE_Y;
					break;
				case 6:
					GIZMO.activeAxis = GZ_ACTIVE_XYZ;
					break;
				}
				GIZMO.activeTransform = data->curTransform;
				GIZMO.startTransform = *data->curTransform;
				GIZMO.startWorldMouse = GetWorldMouse(data,mousepos);
			}
		}
	}

	GIZMO.curAction = action;
}
