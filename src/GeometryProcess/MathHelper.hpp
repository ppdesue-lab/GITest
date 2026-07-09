#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#pragma region math function

struct ContourPoint
{
	glm::vec4 Position;
	glm::vec3 Normal;
	bool isG1 = true;
};


inline glm::mat3 createBasis(glm::vec3 normal)
{
	assert(glm::length(normal) > 0);

	normal = glm::normalize(normal);

	glm::vec3 tmp_x = glm::vec3(1, 0, 0);
	if (glm::distance(tmp_x, normal) < 0.0001f)
		tmp_x = glm::vec3(0, 1, 0);

	glm::vec3 basis_y = glm::normalize(glm::cross(normal, tmp_x));
	glm::vec3 basis_x = glm::normalize(glm::cross(basis_y, normal));
	glm::vec3 basis_z = normal;

	glm::mat3 basis(basis_x, basis_y, basis_z);
	basis = glm::transpose(basis);

	return basis;
};

inline std::vector<glm::vec3> transformPointsToBasis(const std::vector<glm::vec3>& points, glm::vec3 normal)
{
	auto transform = createBasis(normal);

	std::vector<glm::vec3> t_points;
	for (auto pt:points)
	{
		auto t_pt = transform * pt;
		t_points.emplace_back(t_pt);
	}
	return t_points;
};

inline std::vector<glm::vec3> transformPointsBasisBack(const std::vector<glm::vec3>& points, glm::vec3 normal)
{
	auto transform = glm::inverse(createBasis(normal));

	std::vector<glm::vec3> t_points;
	for (auto pt : points)
	{
		auto t_pt = transform * pt;
		t_points.emplace_back(t_pt);
	}
	return t_points;
};

#pragma endregion
