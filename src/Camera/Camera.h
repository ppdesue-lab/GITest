#pragma once

#include <glm/glm.hpp>

class Camera
{
public:
	virtual ~Camera() = default;
	virtual glm::mat4 GetViewMatrix() const = 0;
	virtual glm::mat4 GetProjectionMatrix() const = 0;
	virtual void setInputEnabled(bool enabled) = 0;
	virtual bool isInputEnabled() const = 0;

	virtual void processMouseMovement(float offsetX, float offsetY) = 0;
	virtual void processKeyboard(int forward, int right, int up, float deltaTime) = 0;
};