#pragma once
/**
* @file MathDef.h reference raylib
*/

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thirdparty/glm-aabb/aabb.hpp>

struct Ray{
    glm::vec3 position;
    glm::vec3 direction;
};

struct RayCollision {
    bool hit;               // Did the ray hit something?
    float distance;         // Distance to the nearest hit
    glm::vec3 point;          // Point of the nearest hit
    glm::vec3 normal;         // Surface normal of hit
};

inline glm::vec3 Vector3Project(glm::vec3 v1, glm::vec3 v2)
{
    float v1dv2 = glm::dot(v1, v2);
    float v2dv2 = glm::dot(v2, v2);
    float mag = v1dv2 / v2dv2;
    return v2 * mag;
};

inline RayCollision GetRayCollisionSphere(Ray ray, glm::vec3 center, float radius)
{
    RayCollision collision = { 0 };

    glm::vec3 raySpherePos = (center- ray.position);
    float vector = glm::dot(raySpherePos, ray.direction);
    float distance = glm::length(raySpherePos);
    float d = radius * radius - (distance * distance - vector * vector);

    collision.hit = d >= 0.0f;

    // Check if ray origin is inside the sphere to calculate the correct collision point
    if (distance < radius)
    {
        collision.distance = vector + sqrtf(d);

        // Calculate collision point
        collision.point = (ray.position+ (ray.direction* collision.distance));

        // Calculate collision normal (pointing outwards)
        collision.normal = -(glm::normalize((collision.point- center)));
    }
    else
    {
        collision.distance = vector - sqrtf(d);

        // Calculate collision point
        collision.point = (ray.position+ (ray.direction* collision.distance));

        // Calculate collision normal (pointing inwards)
        collision.normal = glm::normalize((collision.point- center));
    }

    return collision;
}

inline RayCollision GetRayCollisionBox(Ray ray, glm::AABB box)
{
    RayCollision collision = { 0 };

    // Note: If ray.position is inside the box, the distance is negative (as if the ray was reversed)
    // Reversing ray.direction will give use the correct result
    auto box_min = box.getMin();
	auto box_max = box.getMax();

    bool insideBox = (ray.position.x > box_min.x) && (ray.position.x < box_max.x) &&
        (ray.position.y > box_min.y) && (ray.position.y < box_max.y) &&
        (ray.position.z > box_min.z) && (ray.position.z < box_max.z);

    if (insideBox) ray.direction = -(ray.direction);

    float t[11] = { 0 };

    t[8] = 1.0f / ray.direction.x;
    t[9] = 1.0f / ray.direction.y;
    t[10] = 1.0f / ray.direction.z;

    t[0] = (box_min.x - ray.position.x) * t[8];
    t[1] = (box_max.x - ray.position.x) * t[8];
    t[2] = (box_min.y - ray.position.y) * t[9];
    t[3] = (box_max.y - ray.position.y) * t[9];
    t[4] = (box_min.z - ray.position.z) * t[10];
    t[5] = (box_max.z - ray.position.z) * t[10];
    t[6] = (float)fmax(fmax(fmin(t[0], t[1]), fmin(t[2], t[3])), fmin(t[4], t[5]));
    t[7] = (float)fmin(fmin(fmax(t[0], t[1]), fmax(t[2], t[3])), fmax(t[4], t[5]));

    collision.hit = !((t[7] < 0) || (t[6] > t[7]));
    collision.distance = t[6];
    collision.point = (ray.position+ (ray.direction* collision.distance));

    // Get box center point
    collision.normal = glm::mix(box_min, box_max, 0.5f);
    // Get vector center point->hit point
    collision.normal = (collision.point - collision.normal);
    // Scale vector to unit cube
    // NOTE: We use an additional .01 to fix numerical errors
    collision.normal = (collision.normal* 2.01f);
    collision.normal = (collision.normal / (box_max - box_min));
    // The relevant elements of the vector are now slightly larger than 1.0f (or smaller than -1.0f)
    // and the others are somewhere between -1.0 and 1.0 casting to int is exactly our wanted normal!
    collision.normal.x = (float)((int)collision.normal.x);
    collision.normal.y = (float)((int)collision.normal.y);
    collision.normal.z = (float)((int)collision.normal.z);

    collision.normal = glm::normalize(collision.normal);

    if (insideBox)
    {
        // Reset ray.direction
        ray.direction = -(ray.direction);
        // Fix result
        collision.distance *= -1.0f;
        collision.normal = -(collision.normal);
    }

    return collision;
};

inline RayCollision GetRayCollisionTriangle(Ray ray, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3)
{
#define EPSILON 0.000001f        // A small number

    RayCollision collision = { 0 };
    glm::vec3 edge1(0);
    glm::vec3 edge2(0);
    glm::vec3 p, q, tv;
    float det, invDet, u, v, t;

    // Find vectors for two edges sharing V1
    edge1 = (p2 - p1);
    edge2 = (p3 - p1);

    // Begin calculating determinant - also used to calculate u parameter
    p = glm::cross(ray.direction, edge2);

    // If determinant is near zero, ray lies in plane of triangle or ray is parallel to plane of triangle
    det = glm::dot(edge1, p);

    // Avoid culling!
    if ((det > -EPSILON) && (det < EPSILON)) return collision;

    invDet = 1.0f / det;

    // Calculate distance from V1 to ray origin
    tv = (ray.position- p1);

    // Calculate u parameter and test bound
    u = glm::dot(tv, p) * invDet;

    // The intersection lies outside the triangle
    if ((u < 0.0f) || (u > 1.0f)) return collision;

    // Prepare to test v parameter
    q = glm::cross(tv, edge1);

    // Calculate V parameter and test bound
    v = glm::dot(ray.direction, q) * invDet;

    // The intersection lies outside the triangle
    if ((v < 0.0f) || ((u + v) > 1.0f)) return collision;

    t = glm::dot(edge2, q) * invDet;

    if (t > EPSILON)
    {
        // Ray hit, get hit point and normal
        collision.hit = true;
        collision.distance = t;
        collision.normal = glm::normalize(glm::cross(edge1, edge2));
        collision.point = (ray.position + (ray.direction * t));
    }

    return collision;
};

inline RayCollision GetRayCollisionQuad(Ray ray, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 p4)
{
    RayCollision collision = { 0 };

    collision = GetRayCollisionTriangle(ray, p1, p2, p4);

    if (!collision.hit) collision = GetRayCollisionTriangle(ray, p2, p3, p4);

    return collision;
};
//
//inline RayCollision GetRayCollisionSphere(Ray ray, glm::vec3 center, float radius)
//{
//    RayCollision collision = { 0 };
//
//    glm::vec3 raySpherePos = (center - ray.position);
//    float vector = glm::dot(raySpherePos, ray.direction);
//    float distance = glm::length(raySpherePos);
//    float d = radius * radius - (distance * distance - vector * vector);
//
//    collision.hit = d >= 0.0f;
//
//    // Check if ray origin is inside the sphere to calculate the correct collision point
//    if (distance < radius)
//    {
//        collision.distance = vector + sqrtf(d);
//
//        // Calculate collision point
//        collision.point = (ray.position + (ray.direction * collision.distance));
//
//        // Calculate collision normal (pointing outwards)
//        collision.normal = -(glm::normalize((collision.point - center)));
//    }
//    else
//    {
//        collision.distance = vector - sqrtf(d);
//
//        // Calculate collision point
//        collision.point = (ray.position + (ray.direction * collision.distance));
//
//        // Calculate collision normal (pointing inwards)
//        collision.normal = glm::normalize((collision.point - center));
//    }
//
//    return collision;
//};