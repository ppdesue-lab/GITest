#ifndef FPS_CAMERA_H
#define FPS_CAMERA_H

#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <GLM/gtc/quaternion.hpp>
#include <GLM/gtx/quaternion.hpp>

#include "Camera.h"

class FPSCamera : public Camera
{
public:
    FPSCamera();
    FPSCamera(glm::vec3 position, glm::vec3 target, float fov, float aspectRatio);
    
    void update(float deltaTime);
    
    void setPosition(glm::vec3 position);
    glm::vec3 getPosition() const;
    
    void setYaw(float yaw);
    void setPitch(float pitch);
    float getYaw() const;
    float getPitch() const;
    
    void setMovementSpeed(float speed);
    float getMovementSpeed() const;
    
    void setMouseSensitivity(float sensitivity);
    float getMouseSensitivity() const;
    
    void setPitchLimit(float minPitch, float maxPitch);
    
    void lookAt(glm::vec3 target);
    void lookAt(float x, float y, float z);
    
    glm::mat4 GetViewMatrix() const override;
    glm::mat4 GetProjectionMatrix() const override;
    
    void setFOV(float fov);
    float getFOV() const;
    
    void setAspectRatio(float aspectRatio);
    float getAspectRatio() const;
    
    void setNearPlane(float nearPlane);
    void setFarPlane(float farPlane);
    float getNearPlane() const;
    float getFarPlane() const;
    
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const;
    
    void processMouseMovement(float offsetX, float offsetY);
    void processKeyboard(int forward, int right, int up, float deltaTime);

private:
    void updateVectors();

private:
    glm::vec3 m_position;
    glm::vec3 m_target;
    glm::vec3 m_forward;
    glm::vec3 m_right;
    glm::vec3 m_up;
    
    float m_yaw;
    float m_pitch;
    float m_minPitch;
    float m_maxPitch;
    
    float m_movementSpeed;
    float m_mouseSensitivity;
    
    float m_fov;
    float m_aspectRatio;
    float m_nearPlane;
    float m_farPlane;
    
    bool m_inputEnabled;
};

#endif
