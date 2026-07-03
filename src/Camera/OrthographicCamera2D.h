#pragma once

#include "Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class OrthographicCamera2D : public Camera
{
public:
    OrthographicCamera2D(float aspectRatio = 1.0f, float orthoHeight = 200.0f);

    glm::mat4 GetViewMatrix() const override;
    glm::mat4 GetProjectionMatrix() const override;

    void setAspectRatio(float aspectRatio) override;
    void setInputEnabled(bool enabled) override;
    bool isInputEnabled() const override;

    void processMouseMovement(float x, float y) override;
    void processKeyboard(int forward, int right, int up, float deltaTime) override;

    float getNearPlane() const override;
    float getFarPlane() const override;

    void PanPixels(const glm::vec2& deltaPixels, const glm::vec2& viewportSize);
    void Zoom(float factor);
    void ResetView();
    const glm::vec2& GetCenter() const { return m_Center; }
    float GetOrthoHeight() const { return m_OrthoHeight; }

private:
    glm::vec2 ScreenDeltaToWorldDelta(const glm::vec2& deltaPixels, const glm::vec2& viewportSize) const;

private:
    glm::vec2 m_Center = glm::vec2(0.0f);
    float m_AspectRatio = 1.0f;
    float m_OrthoHeight = 200.0f;
    float m_DefaultOrthoHeight = 200.0f;
    float m_NearPlane = -10000.0f;
    float m_FarPlane = 10000.0f;
    float m_MovementSpeed = 100.0f;
    bool m_InputEnabled = false;
};
