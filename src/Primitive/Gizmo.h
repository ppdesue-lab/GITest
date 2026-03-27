
#ifndef GIZMO_H
#define GIZMO_H

//--------------------------------------------------------------------------------------------------
// Includes
//--------------------------------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <Transform.h>


/**
 * Bitwise flags for configuring DrawGizmo3D().
 * Use these flags to customize specific gizmo behaviors.
 * @note Mutually exclusive axis orientation flags (e.g., GIZMO_LOCAL, GIZMO_VIEW)
 * override the default global axis behavior.
 */
typedef enum
{
	GIZMO_DISABLED	= 0,		// 0: Disables gizmo drawing

	// Bitwise flags
	GIZMO_TRANSLATE = 1 << 0,	// Enables translation gizmo
	GIZMO_ROTATE	= 1 << 1,	// Enables rotation gizmo
	GIZMO_SCALE		= 1 << 2,	// Enables scaling gizmo (implicitly enables GIZMO_LOCAL)
	GIZMO_ALL		= GIZMO_TRANSLATE | GIZMO_ROTATE | GIZMO_SCALE,		// Enables all gizmos

	// Mutually exclusive axis orientation flags
	// Default: Global axis orientation
	GIZMO_LOCAL		= 1 << 3,	// Orients axes locally
	GIZMO_VIEW		= 1 << 4	// Orients axes based on screen view
} GizmoFlags;

//struct Color {
//    unsigned char r;        // Color red value
//    unsigned char g;        // Color green value
//    unsigned char b;        // Color blue value
//    unsigned char a;        // Color alpha value
//};

typedef glm::vec4 Color;

//--------------------------------------------------------------------------------------------------
// GIZMO API
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------

#if defined(__cplusplus)
extern "C" {
#endif

//--------------------------------------------------------------------------------------------------

	/**
	 * Initialize a gizmo Transform with default values.
	 * @return A Transform initialized to default values.
	 */
	Transform GizmoIdentity(void);

	/**
	 * Convert a gizmo Transform to the corresponding Matrix.
	 * @param transform The gizmo Transform to convert.
	 * @return A Matrix built from the Transform values.
	 */
	glm::mat4 GizmoToMatrix(Transform transform);

	/**
	 * Draw the gizmo on the screen in an immediate-mode style.
	 * @param flags A combination of GizmoFlags to configure gizmo behavior.
	 * @param transform A pointer to the Transform affected by the gizmo.
	 * @return true if the gizmo is active and affecting the transform; false otherwise.
	 */
	bool DrawGizmo3D(const glm::mat4& camView, const glm::mat4& camProj,
		bool leftdown, glm::vec2 mousepos
		, int flags, Transform* transform);

	/**
	 * Set the size of the gizmo.
	 * @param size The new size of the gizmo.
	 * @note All internal gizmo metrics are expressed as a fraction of this measure.
	 * @default 1.5f
	 */
	void SetGizmoSize(float size);

	/**
	 * Set the line width of the gizmo geometry.
	 * @param width The new line width.
	 * @default 2.5f
	 */
	void SetGizmoLineWidth(float width);

	/**
	 * Set the viewport size for correct gizmo scaling.
	 * @param width The width of the viewport in pixels.
	 * @param height The height of the viewport in pixels.
	 * @note This function should be called whenever the viewport size changes to maintain consistent gizmo scaling.
	 */
	void SetViewportSize(int width, int height);

	/**
	 * Set the colors used by the gizmo.
	 * @param x Color of the X-axis.
	 * @param y Color of the Y-axis.
	 * @param z Color of the Z-axis.
	 * @param center Color of the central circle.
	 * @default {229, 72, 91, 255}, {131, 205, 56, 255}, {69, 138, 242, 255}, {255, 255, 255, 200}
	 */
	void SetGizmoColors(Color x, Color y, Color z, Color center);

	/**
	 * Change the global axis orientation.
	 * @param right Direction of the right vector.
	 * @param up Direction of the up vector.
	 * @param forward Direction of the forward vector.
	 * @note The vectors should be orthogonal to each other for consistent behavior.
	 * @default (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
	 */
	void SetGizmoGlobalAxis(glm::vec3 right, glm::vec3 up, glm::vec3 forward);


//--------------------------------------------------------------------------------------------------

#if defined(__cplusplus)
}
#endif

//--------------------------------------------------------------------------------------------------

#endif  // GIZMO_H