#include "OrthographicCamera2D.h"

#include <glm/common.hpp>

OrthographicCamera2D::OrthographicCamera2D(float aspectRatio, float orthoHeight)
    : m_AspectRatio(aspectRatio)
    , m_OrthoHeight(orthoHeight)
    , m_DefaultOrthoHeight(orthoHeight)
{
}

glm::mat4 OrthographicCamera2D::GetViewMatrix() const
{
    const glm::vec3 eye(m_Center.x, m_Center.y, 1000.0f);
    return glm::lookAt(eye, eye + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrthographicCamera2D::GetProjectionMatrix() const
{
    const float halfHeight = m_OrthoHeight * 0.5f;
    const float halfWidth = halfHeight * m_AspectRatio;
    return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_NearPlane, m_FarPlane);
}

void OrthographicCamera2D::setAspectRatio(float aspectRatio)
{
    m_AspectRatio = glm::max(aspectRatio, 0.0001f);
}

void OrthographicCamera2D::setInputEnabled(bool enabled)
{
    m_InputEnabled = enabled;
}

bool OrthographicCamera2D::isInputEnabled() const
{
    return m_InputEnabled;
}

void OrthographicCamera2D::processMouseMovement(float, float)
{
}

void OrthographicCamera2D::processKeyboard(int forward, int right, int, float deltaTime)
{
    const float velocity = m_MovementSpeed * deltaTime;
    m_Center.x += (float)right * velocity;
    m_Center.y += (float)forward * velocity;
}

float OrthographicCamera2D::getNearPlane() const
{
    return m_NearPlane;
}

float OrthographicCamera2D::getFarPlane() const
{
    return m_FarPlane;
}

void OrthographicCamera2D::PanPixels(const glm::vec2& deltaPixels, const glm::vec2& viewportSize)
{
    m_Center += ScreenDeltaToWorldDelta(deltaPixels, viewportSize);
}

void OrthographicCamera2D::Zoom(float factor)
{
    m_OrthoHeight = glm::clamp(m_OrthoHeight * factor, 1.0f, 100000.0f);
}

void OrthographicCamera2D::ResetView()
{
    m_Center = glm::vec2(0.0f);
    m_OrthoHeight = m_DefaultOrthoHeight;
}

glm::vec2 OrthographicCamera2D::ScreenDeltaToWorldDelta(const glm::vec2& deltaPixels, const glm::vec2& viewportSize) const
{
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        return glm::vec2(0.0f);

    const float worldPerPixelY = m_OrthoHeight / viewportSize.y;
    const float worldPerPixelX = (m_OrthoHeight * m_AspectRatio) / viewportSize.x;
    return glm::vec2(-deltaPixels.x * worldPerPixelX, deltaPixels.y * worldPerPixelY);
}
