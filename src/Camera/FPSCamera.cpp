#include "FPSCamera.h"

FPSCamera::FPSCamera()
    : m_position(0.0f, 0.0f, 3.0f)
    , m_target(0.0f, 0.0f, 0.0f)
    , m_forward(0.0f, 0.0f, -1.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_yaw(-90.0f)
    , m_pitch(0.0f)
    , m_minPitch(-89.0f)
    , m_maxPitch(89.0f)
    , m_movementSpeed(5.0f)
    , m_mouseSensitivity(0.1f)
    , m_fov(45.0f)
    , m_aspectRatio(16.0f / 9.0f)
    , m_nearPlane(0.1f)
    , m_farPlane(1000.0f)
    , m_inputEnabled(true)
{
    updateVectors();
}

FPSCamera::FPSCamera(glm::vec3 position, glm::vec3 target, float fov, float aspectRatio)
    : m_position(position)
    , m_target(target)
    , m_forward(0.0f, 0.0f, -1.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_yaw(-90.0f)
    , m_pitch(0.0f)
    , m_minPitch(-89.0f)
    , m_maxPitch(89.0f)
    , m_movementSpeed(5.0f)
    , m_mouseSensitivity(0.1f)
    , m_fov(fov)
    , m_aspectRatio(aspectRatio)
    , m_nearPlane(0.1f)
    , m_farPlane(1000.0f)
    , m_inputEnabled(true)
{
    updateVectors();
}

void FPSCamera::update(float deltaTime)
{
    updateVectors();
}

void FPSCamera::setPosition(glm::vec3 position)
{
    m_position = position;
}

glm::vec3 FPSCamera::getPosition() const
{
    return m_position;
}

void FPSCamera::setYaw(float yaw)
{
    m_yaw = yaw;
    if (m_yaw > 180.0f) m_yaw -= 360.0f;
    if (m_yaw < -180.0f) m_yaw += 360.0f;
}

void FPSCamera::setPitch(float pitch)
{
    m_pitch = glm::clamp(pitch, m_minPitch, m_maxPitch);
}

float FPSCamera::getYaw() const
{
    return m_yaw;
}

float FPSCamera::getPitch() const
{
    return m_pitch;
}

void FPSCamera::setMovementSpeed(float speed)
{
    m_movementSpeed = speed;
}

float FPSCamera::getMovementSpeed() const
{
    return m_movementSpeed;
}

void FPSCamera::setMouseSensitivity(float sensitivity)
{
    m_mouseSensitivity = sensitivity;
}

float FPSCamera::getMouseSensitivity() const
{
    return m_mouseSensitivity;
}

void FPSCamera::setPitchLimit(float minPitch, float maxPitch)
{
    m_minPitch = minPitch;
    m_maxPitch = maxPitch;
}

void FPSCamera::lookAt(glm::vec3 target)
{
    m_target = target;
    m_forward = glm::normalize(m_target - m_position);
    
    float yawRad = glm::atan(m_forward.z, m_forward.x);
    float pitchRad = glm::asin(m_forward.y);
    
    m_yaw = glm::degrees(yawRad);
    m_pitch = glm::degrees(pitchRad);
    
    updateVectors();
}

void FPSCamera::lookAt(float x, float y, float z)
{
    lookAt(glm::vec3(x, y, z));
}

glm::mat4 FPSCamera::getViewMatrix() const
{
    return glm::lookAt(m_position, m_position + m_forward, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 FPSCamera::getProjectionMatrix() const
{
    return glm::perspective(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
}

void FPSCamera::setFOV(float fov)
{
    m_fov = fov;
}

float FPSCamera::getFOV() const
{
    return m_fov;
}

void FPSCamera::setAspectRatio(float aspectRatio)
{
    m_aspectRatio = aspectRatio;
}

float FPSCamera::getAspectRatio() const
{
    return m_aspectRatio;
}

void FPSCamera::setNearPlane(float nearPlane)
{
    m_nearPlane = nearPlane;
}

void FPSCamera::setFarPlane(float farPlane)
{
    m_farPlane = farPlane;
}

float FPSCamera::getNearPlane() const
{
    return m_nearPlane;
}

float FPSCamera::getFarPlane() const
{
    return m_farPlane;
}

glm::vec3 FPSCamera::getForward() const
{
    return m_forward;
}

glm::vec3 FPSCamera::getRight() const
{
    return m_right;
}

glm::vec3 FPSCamera::getUp() const
{
    return m_up;
}

void FPSCamera::setInputEnabled(bool enabled)
{
    m_inputEnabled = enabled;
}

bool FPSCamera::isInputEnabled() const
{
    return m_inputEnabled;
}

void FPSCamera::processMouseMovement(float offsetX, float offsetY)
{
    if (!m_inputEnabled) return;
    
    m_yaw += offsetX * m_mouseSensitivity;
    m_pitch -= offsetY * m_mouseSensitivity;
    
    if (m_yaw > 180.0f) m_yaw -= 360.0f;
    if (m_yaw < -180.0f) m_yaw += 360.0f;
    
    m_pitch = glm::clamp(m_pitch, m_minPitch, m_maxPitch);
    
    updateVectors();
}

void FPSCamera::processKeyboard(int forward, int right, int up, float deltaTime)
{
    if (!m_inputEnabled) return;
    
    float velocity = m_movementSpeed * deltaTime;
    
    if (forward > 0)
        m_position += m_forward * velocity;
    else if (forward < 0)
        m_position -= m_forward * velocity;
    
    if (right > 0)
        m_position += m_right * velocity;
    else if (right < 0)
        m_position -= m_right * velocity;
    
    if (up > 0)
        m_position += glm::vec3(0.0f, 1.0f, 0.0f) * velocity;
    else if (up < 0)
        m_position -= glm::vec3(0.0f, 1.0f, 0.0f) * velocity;
}

void FPSCamera::updateVectors()
{
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);
    
    m_forward.x = glm::cos(yawRad) * glm::cos(pitchRad);
    m_forward.y = glm::sin(pitchRad);
    m_forward.z = glm::sin(yawRad) * glm::cos(pitchRad);
    m_forward = glm::normalize(m_forward);
    
    m_right = glm::normalize(glm::cross(m_forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    
    m_up = glm::normalize(glm::cross(m_right, m_forward));
}
