#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>


struct Transform {
    glm::vec3 translation = glm::vec3(0,0,0);    // Translation
    glm::quat rotation = glm::quat(1,0,0,0);    // Rotation
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

	Transform operator*(const Transform& other) const {
		Transform result;

		// 缩放组合
		result.scale = scale * other.scale;

		// 旋转组合
		result.rotation = rotation * other.rotation;

		// 平移组合 (关键部分)
		// 先对 other.translation 应用 self 的缩放，再应用 self 的旋转，最后加上 self 的平移
		result.translation = translation + glm::rotate(rotation, /*scale **/ other.translation);

		return result;
	}

	Transform operator/(const Transform& other) const {
		Transform result;

		// 缩放除法
		result.scale = scale / other.scale;

		// 旋转除法 (先对 other 求逆)
		result.rotation = glm::inverse(other.rotation) * rotation;

		// 平移除法 (移除 other 的旋转和缩放影响)
		result.translation = translation - glm::rotate(result.rotation, other.translation);

		return result;
	}
};