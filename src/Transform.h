#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>


struct Transform {
    glm::vec3 translation = glm::vec3(0,0,0);    // Translation
    glm::quat rotation = glm::quat(0,0,0,1);    // Rotation
    glm::vec3 scale = glm::vec3(0,0,0);          // Scale


    void Move(const glm::vec3& delta) {
        translation += delta;
	}

    void Rotate(const glm::quat& delta) {
        rotation = delta * rotation;
    }
    void Scale(const glm::vec3& delta) {
        scale += delta;
	}

    glm::mat4 GetMatrix() const {
        glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 rotationMatrix = glm::toMat4(rotation);
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
        return translationMatrix * rotationMatrix * scaleMatrix;
	}
};