#pragma once
// Lightweight C++ port of code-python/slicer.py using GLM for vector math.
// Implements incremental slicing of triangle meshes into contour polygons.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include "glm-aabb/aabb.hpp"
#include "MathHelper.hpp"

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

namespace slicing {

constexpr float EPS_DEFAULT = 0.0002f;
constexpr float MIN_SEGMENT_LEN = 0.0001f;
constexpr double PI_D = 3.14159265358979323846;

enum Orientation { CLOCKWISE = -1, COUNTERCLOCKWISE = 1 };
enum PointInPolygon { OUTSIDE_POLYGON = 0, INSIDE_POLYGON = 1 };

struct Triangle {
    glm::vec3 v0{};
    glm::vec3 v1{};
    glm::vec3 v2{};

    float min_z() const {
        return std::min(v0.z, std::min(v1.z, v2.z));
    }

    float max_z() const {
        return std::max(v0.z, std::max(v1.z, v2.z));
    }
};

inline float mround_scalar(float v, float eps, int mod, int rem) {
    float y = std::round(v / (mod * eps));
    return (y * mod + rem) * eps;
};

inline glm::vec3 mround_vec(const glm::vec3& v, float eps, int mod, int rem) {
    return glm::vec3(
        mround_scalar(v.x, eps, mod, rem),
        mround_scalar(v.y, eps, mod, rem),
        mround_scalar(v.z, eps, mod, rem));
};

inline float triangle_signed_area(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    float x0 = v0.x;
    float y0 = v0.y;
    float x1 = v1.x;
    float y1 = v1.y;
    float x2 = v2.x;
    float y2 = v2.y;
    return ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) * 0.5f;
};

inline float vectors_angle(const glm::vec3& o, const glm::vec3& v1, const glm::vec3& v2) {
    glm::vec3 u = v1 - o;
    glm::vec3 w = v2 - o;

    float ulen = glm::length(u);
    float wlen = glm::length(w);
    if (ulen == 0.0f || wlen == 0.0f) {
        return 0.0f;
    }

    u /= ulen;
    w /= wlen;

    float cos_theta = std::clamp(glm::dot(u, w), -1.0f, 1.0f);
    float angle = std::acos(cos_theta);
    float cross_z = glm::cross(u, w).z;
    return (cross_z >= 0.0f ? angle : -angle);
};

struct Polygon {
    std::vector<glm::vec3> vertices;

    float signed_area() const {
        if (vertices.size() < 3) {
            return 0.0f;
        }
        const glm::vec3& anchor = vertices.front();
        float area = 0.0f;
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i) {
            area += triangle_signed_area(anchor, vertices[i], vertices[i + 1]);
        }
        return area;
    }

    Orientation orientation() const {
        return signed_area() > 0.0f ? COUNTERCLOCKWISE : CLOCKWISE;
    }

    void invert_orientation() {
        std::reverse(vertices.begin(), vertices.end());
    }

    PointInPolygon is_inside(const glm::vec3& p) const {
        double winding = 0.0;
        const std::size_t n = vertices.size();
        if (n < 3) {
            return OUTSIDE_POLYGON;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const glm::vec3& a = vertices[i];
            const glm::vec3& b = vertices[(i + 1) % n];
            winding += vectors_angle(p, a, b);
        }
        return (std::fabs(winding - 2.0 * PI_D) < std::fabs(winding) ? INSIDE_POLYGON : OUTSIDE_POLYGON);
    }
};

struct VertexKey {
    int x{};
    int y{};
    int z{};
    bool operator==(const VertexKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& k) const noexcept {
        std::size_t h = std::hash<int>()(k.x);
        h ^= std::hash<int>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        //h ^= std::hash<int>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

inline VertexKey make_key(const glm::vec3& v, float eps) {
    return VertexKey{ static_cast<int>(std::lround(v.x / eps)), static_cast<int>(std::lround(v.y / eps)), static_cast<int>(std::lround(v.z / eps)) };
};

inline std::vector<Triangle> LoadTrianglesFromFile(const std::string& filepath,const glm::vec3& normal)
{
    //1 load model with assimp -> std::vector<glm::vec3> tri_pts 
	std::vector<glm::vec3> tri_pts;
	Assimp::Importer importer;

	auto fullpath = std::string(APP_ROOT_PATH) + "/"+ filepath;
	const aiScene* scene = importer.ReadFile(fullpath, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);
	if (!scene || !scene->HasMeshes()) {
		std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
		return std::vector<Triangle>();
	}
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
		aiMesh* mesh = scene->mMeshes[m];
		// Extract vertices
		std::vector<glm::vec3> tri_local_pts;
		for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
			aiVector3D vertex = mesh->mVertices[v];
			tri_local_pts.emplace_back(glm::vec3(vertex.x, vertex.y, vertex.z));
		}
		// Extract indices
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
			aiFace face = mesh->mFaces[f];
			for (unsigned int i = 0; i < face.mNumIndices; ++i) {
				//tri_indices.push_back(face.mIndices[i] + tri_pts.size() - mesh->mNumVertices); // Adjust index based on current vertex count
				auto tri_pt = tri_local_pts[face.mIndices[i]];
				tri_pts.emplace_back(tri_pt);
			}
		}
	}

    //offset xz to center
    // 1. 计算所有SavedContours的AABB
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();


    for (const auto& tri : tri_pts) {

        minX = std::min(minX, tri.x);
        maxX = std::max(maxX, tri.x);
        minY = std::min(minY, tri.y);
        maxY = std::max(maxY, tri.y);
        minZ = std::min(minZ, tri.z);
        maxZ = std::max(maxZ, tri.z);
        
    }
    // 2. 计算AABB中心轴 (x, y) 方向，z轴为层高
    float centerX = 0.5f * (minX + maxX);
    float centerZ = 0.5f * (minZ + maxZ);
    //offset all points to center
    for (auto& tri : tri_pts) {
        tri.x -= centerX;
        tri.z -= centerZ;
    }


	//2 slice triangles

	auto tri_pts_all = transformPointsToBasis(tri_pts, normal);
	glm::AABB tri_pts_box;
	for (auto pt : tri_pts_all)
	{
		tri_pts_box.extend(pt);
	}
    std::vector<Triangle> triangles;
    for (std::size_t i = 0; i + 2 < tri_pts_all.size(); i += 3) {
        triangles.push_back(Triangle{ tri_pts_all[i], tri_pts_all[i + 1], tri_pts_all[i + 2] });
    }
    return triangles;
};
class Slicer {
public:
    Slicer(std::vector<Triangle> triangles, std::vector<float> planes, float delta, bool sorted)
        : triangles_(std::move(triangles)), planes_(std::move(planes)), delta_(delta), sorted_(sorted), eps_(EPS_DEFAULT) {
        round_triangles_even();
        delta_ = mround_scalar(delta_, eps_, 2, 0);
        if (delta_ > 0.0f) {
            uniform_planes();
        }
    }

    void incremental_slicing() {
        std::vector<std::vector<std::size_t>> L = build_triangle_list();
        std::vector<std::size_t> active;

        segments_.clear();
        segments_.reserve(planes_.size());

        for (std::size_t i = 0; i < planes_.size(); ++i) {
            segments_.emplace_back();
            active.insert(active.end(), L[i].begin(), L[i].end());

            std::size_t t = 0;
            while (t < active.size()) {
                const Triangle& tri = triangles_[active[t]];
                if (tri.max_z() < planes_[i]) {
                    active.erase(active.begin() + static_cast<long>(t));
                    continue;
                }

                std::pair<glm::vec3, glm::vec3> inter;
                if (compute_intersection(tri, planes_[i], inter)) {
                    segments_.back().push_back(inter);
                }
                ++t;
            }
        }

        contour_construction();
    }

    const std::vector<std::vector<Polygon>>& planes_with_polygons() const {
        return plane_polygons_;
    }

    const std::vector<std::vector<std::pair<glm::vec3, glm::vec3>>>& segments() const {
        return segments_;
    }

    // Static helper: build oriented contours from plain segments (each entry has two endpoints).
    static std::vector<std::vector<glm::vec3>> ContourConstrution(
        const std::vector<std::vector<glm::vec3>>& contour_segments,
        float eps = EPS_DEFAULT) {

        std::vector<std::vector<glm::vec3>> polygons;

        // Hash endpoints and build adjacency buckets.
        std::unordered_map<VertexKey, std::vector<glm::vec3>, VertexKeyHash> H;
        H.reserve(contour_segments.size() * 2);

        for (const auto& seg : contour_segments) {
            if (seg.size() < 2) {
                continue;
            }
            glm::vec3 a = seg[0];
            glm::vec3 b = seg[1];

            auto round_xy = [&](const glm::vec3& v) {
                glm::vec3 r = v;
                r.x = mround_scalar(r.x, eps, 2, 1);
                r.y = mround_scalar(r.y, eps, 2, 1);
                return r;
            };

            a = round_xy(a);
            b = round_xy(b);

            if (glm::length2(a - b) <= MIN_SEGMENT_LEN * MIN_SEGMENT_LEN) {
                continue;
            }

            auto add_vertex = [&](const glm::vec3& key_vertex, const glm::vec3& neighbor) {
                VertexKey key = make_key(key_vertex, eps);
                auto& bucket = H[key];
                if (bucket.empty()) {
                    bucket.push_back(key_vertex); // store current vertex
                }
                bucket.push_back(neighbor);
            };

            add_vertex(a, b);
            add_vertex(b, a);
        }

        while (!H.empty()) {
            auto it = H.begin();
            VertexKey u_key = it->first;
            std::vector<glm::vec3> u_bucket = std::move(it->second);
            H.erase(it);
            if (u_bucket.size() < 3) {
                continue;
            }

            glm::vec3 u_vertex = u_bucket[0];
            glm::vec3 final_vertex = u_bucket[2];
            VertexKey final_key = make_key(final_vertex, eps);
            glm::vec3 v_vertex = u_bucket[1];
            VertexKey v_key = make_key(v_vertex, eps);

            std::vector<glm::vec3> polygon;
            polygon.push_back(u_vertex);

            while (!(u_key == final_key)) {
                VertexKey last_key = u_key;

                auto v_it = H.find(v_key);
                if (v_it == H.end() || v_it->second.size() < 2) {
                    break;
                }

                u_key = v_key;
                u_vertex = v_it->second[0];

                glm::vec3 candidate1 = v_it->second[1];
                glm::vec3 candidate2 = (v_it->second.size() > 2) ? v_it->second[2] : v_it->second[1];
                VertexKey candidate1_key = make_key(candidate1, eps);

                if (!(candidate1_key == last_key)) {
                    v_vertex = candidate1;
                    v_key = candidate1_key;
                } else {
                    v_vertex = candidate2;
                    v_key = make_key(candidate2, eps);
                }

                H.erase(v_it);
                polygon.push_back(u_vertex);
            }

            polygons.push_back(std::move(polygon));
        }

        return polygons;
    }

    glm::vec3 min_coordinates() const {
        glm::vec3 mval(std::numeric_limits<float>::max());
        for (const Triangle& t : triangles_) {
            mval = glm::min(mval, t.v0);
            mval = glm::min(mval, t.v1);
            mval = glm::min(mval, t.v2);
        }
        return mval;
    }

    glm::vec3 max_coordinates() const {
        glm::vec3 mval(std::numeric_limits<float>::lowest());
        for (const Triangle& t : triangles_) {
            mval = glm::max(mval, t.v0);
            mval = glm::max(mval, t.v1);
            mval = glm::max(mval, t.v2);
        }
        return mval;
    }

    std::pair<std::vector<float>, std::size_t> OpenGLPlanesData(float mmin, float mmax, float bmin, float bmax) const {
        std::vector<float> normalized_planes;
        normalized_planes.reserve(planes_.size());
        for (float p : planes_) {
            normalized_planes.push_back(((p - mmin) / (mmax - mmin)) * (bmax - bmin) + bmin);
        }

        const int step = 2;
        float triangles_count = (2.0f / step) * (2.0f / step) * 2.0f * static_cast<float>(normalized_planes.size());
        std::size_t nvertices = static_cast<std::size_t>(triangles_count * 3.0f);
        std::vector<float> data(nvertices * 6, 0.0f);

        std::size_t idx = 0;
        for (float p : normalized_planes) {
            for (int j = -1; j < 1; j += step) {
                for (int k = -1; k < 1; k += step) {
                    data[idx++] = static_cast<float>(j);
                    data[idx++] = static_cast<float>(k);
                    data[idx++] = p;
                    data[idx++] = 0.0f;
                    data[idx++] = 1.0f;
                    data[idx++] = 0.0f;

                    data[idx++] = static_cast<float>(j);
                    data[idx++] = static_cast<float>(k + step);
                    data[idx++] = p;
                    data[idx++] = 0.0f;
                    data[idx++] = 1.0f;
                    data[idx++] = 0.0f;

                    data[idx++] = static_cast<float>(j + step);
                    data[idx++] = static_cast<float>(k);
                    data[idx++] = p;
                    data[idx++] = 0.0f;
                    data[idx++] = 1.0f;
                    data[idx++] = 0.0f;

                    data[idx++] = static_cast<float>(j);
                    data[idx++] = static_cast<float>(k + step);
                    data[idx++] = p;
                    data[idx++] = 0.0f;
                    data[idx++] = 1.0f;
                    data[idx++] = 0.0f;

                    data[idx++] = static_cast<float>(j + step);
                    data[idx++] = static_cast<float>(k + step);
                    data[idx++] = p;
                    data[idx++] = 0.0f;
                    data[idx++] = 1.0f;
                    data[idx++] = 0.0f;

                    data[idx++] = static_cast<float>(j + step);
                    data[idx++] = static_cast<float>(k);
                    data[idx++] = p;
                    data[idx++] = 0.0f;
                    data[idx++] = 1.0f;
                    data[idx++] = 0.0f;
                }
            }
        }
        return {data, nvertices};
    }

    std::pair<std::vector<float>, std::size_t> OpenGLPolygonsData(const glm::vec3& mmin, const glm::vec3& mmax, float bmin, float bmax) const {
        std::size_t nsegments = 0;
        for (const auto& plane : plane_polygons_) {
            for (const auto& poly : plane) {
                nsegments += 2 * poly.vertices.size();
            }
        }
        std::size_t nvertices = nsegments * 2;
        std::vector<float> data(nsegments * 2 * 6, 0.0f);

        std::size_t j = 0;
        for (const auto& plane : plane_polygons_) {
            for (const auto& poly : plane) {
                glm::vec3 color = (poly.orientation() == COUNTERCLOCKWISE) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
                const std::size_t n = poly.vertices.size();
                for (std::size_t v = 0; v < n; ++v) {
                    glm::vec3 a = ((poly.vertices[v] - mmin) / (mmax - mmin)) * (bmax - bmin) + glm::vec3(bmin);
                    glm::vec3 b = ((poly.vertices[(v + 1) % n] - mmin) / (mmax - mmin)) * (bmax - bmin) + glm::vec3(bmin);

                    data[j++] = a.x;
                    data[j++] = a.y;
                    data[j++] = a.z;
                    data[j++] = color.r;
                    data[j++] = color.g;
                    data[j++] = color.b;

                    data[j++] = b.x;
                    data[j++] = b.y;
                    data[j++] = b.z;
                    data[j++] = color.r;
                    data[j++] = color.g;
                    data[j++] = color.b;
                }
            }
        }
        return {data, nvertices};
    }

private:
    void round_triangles_even() {
        for (Triangle& t : triangles_) {
            t.v0 = mround_vec(t.v0, eps_, 2, 0);
            t.v1 = mround_vec(t.v1, eps_, 2, 0);
            t.v2 = mround_vec(t.v2, eps_, 2, 0);
        }
    }

    std::vector<std::vector<std::size_t>> build_triangle_list() const {
        std::vector<std::vector<std::size_t>> L(planes_.size() + 1);
        if (delta_ > 0.0f) {
            for (std::size_t t = 0; t < triangles_.size(); ++t) {
                float minz = triangles_[t].min_z();
                std::size_t idx;
                if (minz < planes_.front()) {
                    idx = 0;
                } else if (minz > planes_.back()) {
                    idx = planes_.size();
                } else {
                    idx = static_cast<std::size_t>(std::floor((minz - planes_.front()) / delta_)) + 1U;
                }
                L[idx].push_back(t);
            }
        } else {
            for (std::size_t t = 0; t < triangles_.size(); ++t) {
                std::size_t idx = binary_search(t);
                L[idx].push_back(t);
            }
        }
        return L;
    }

    std::size_t binary_search(std::size_t tri_idx) const {
        float minz = triangles_[tri_idx].min_z();
        if (minz > planes_.back()) {
            return planes_.size();
        }
        if (minz < planes_.front()) {
            return 0;
        }
        std::size_t l = 0;
        std::size_t r = planes_.size() - 1;
        while (r - l > 1) {
            std::size_t m = (l + r) / 2;
            if (minz > planes_[m]) {
                l = m;
            } else {
                r = m;
            }
        }
        return r;
    }

    bool compute_intersection(const Triangle& t, float p, std::pair<glm::vec3, glm::vec3>& out) const {
        std::vector<glm::vec3> intersection(2, glm::vec3(0.0f));
        int n = 0;

        auto add_if_intersects = [&](const glm::vec3& a, const glm::vec3& b) {
            glm::vec3 v = b - a;
            if (std::fabs(v.z) > 0.0f) {
                float alpha = (p - a.z) / v.z;
                if (alpha >= 0.0f && alpha <= 1.0f && n < 2) {
                    intersection[n++] = glm::vec3(a.x + alpha * v.x, a.y + alpha * v.y, p);
                }
            }
        };

        add_if_intersects(t.v0, t.v1);
        add_if_intersects(t.v0, t.v2);
        add_if_intersects(t.v1, t.v2);

        if (n == 2) {
            out = {intersection[0], intersection[1]};
            return true;
        }
        return false;
    }

    void contour_construction() {
        plane_polygons_.clear();
        plane_polygons_.reserve(segments_.size());

        for (std::size_t i = 0; i < segments_.size(); ++i) {
            plane_polygons_.emplace_back();

            for (auto& seg : segments_[i]) {
                auto round_xy = [&](const glm::vec3& v) {
                    glm::vec3 r = v;
                    r.x = mround_scalar(r.x, eps_, 2, 1);
                    r.y = mround_scalar(r.y, eps_, 2, 1);
                    return r;
                };
                seg.first = round_xy(seg.first);
                seg.second = round_xy(seg.second);
            }

            std::unordered_map<VertexKey, std::vector<glm::vec3>, VertexKeyHash> H;
            H.reserve(segments_[i].size() * 2);

            for (const auto& seg : segments_[i]) {
                if (glm::length2(seg.first - seg.second) > MIN_SEGMENT_LEN * MIN_SEGMENT_LEN) {
                    auto add_vertex = [&](const glm::vec3& key_vertex, const glm::vec3& neighbor) {
                        VertexKey key = make_key(key_vertex, eps_);
                        auto& bucket = H[key];
                        if (bucket.empty()) {
                            bucket.push_back(key_vertex); // store current vertex
                        }
                        bucket.push_back(neighbor);
                    };
                    add_vertex(seg.first, seg.second);
                    add_vertex(seg.second, seg.first);
                }
            }

            while (!H.empty()) {
                auto it = H.begin();
                VertexKey u_key = it->first;
                std::vector<glm::vec3> u_bucket = std::move(it->second);
                H.erase(it);
                if (u_bucket.size() < 3) {
                    continue;
                }

                glm::vec3 u_vertex = u_bucket[0];
                glm::vec3 final_vertex = u_bucket[2];
                VertexKey final_key = make_key(final_vertex, eps_);
                glm::vec3 v_vertex = u_bucket[1];
                VertexKey v_key = make_key(v_vertex, eps_);

                Polygon polygon;
                polygon.vertices.push_back(u_vertex);

                while (!(u_key == final_key)) {
                    VertexKey last_key = u_key;
                    glm::vec3 last_vertex = u_vertex;

                    auto v_it = H.find(v_key);
                    if (v_it == H.end() || v_it->second.size() < 2) {
                        break;
                    }

                    u_key = v_key;
                    u_vertex = v_it->second[0];

                    glm::vec3 candidate1 = v_it->second[1];
                    glm::vec3 candidate2 = (v_it->second.size() > 2) ? v_it->second[2] : v_it->second[1];
                    VertexKey candidate1_key = make_key(candidate1, eps_);

                    if (!(candidate1_key == last_key)) {
                        v_vertex = candidate1;
                        v_key = candidate1_key;
                    } else {
                        v_vertex = candidate2;
                        v_key = make_key(candidate2, eps_);
                    }

                    H.erase(v_it);
                    polygon.vertices.push_back(u_vertex);
                }

                plane_polygons_.back().push_back(std::move(polygon));
            }
        }

        for (auto& plane : plane_polygons_) {
            for (std::size_t p1 = 0; p1 < plane.size(); ++p1) {
                Orientation desired = COUNTERCLOCKWISE;
                for (std::size_t p2 = 0; p2 < plane.size(); ++p2) {
                    if (p1 == p2) {
                        continue;
                    }
                    if (!plane[p1].vertices.empty() && plane[p2].is_inside(plane[p1].vertices[0]) == INSIDE_POLYGON) {
                        desired = CLOCKWISE;
                        break;
                    }
                }
                if (desired != plane[p1].orientation()) {
                    plane[p1].invert_orientation();
                }
            }
        }
    }

    void uniform_planes() {
        float z_min = min_coordinates().z;
        float z_max = max_coordinates().z;
        float P0 = mround_scalar(z_min - delta_, eps_, 2, 1);

        planes_.clear();
        int i = 1;
        float Pi = P0 + i * delta_;
        while (Pi < z_max) {
            if (Pi > z_min) {
                planes_.push_back(Pi);
            }
            ++i;
            Pi = P0 + i * delta_;
        }
    }

private:
    std::vector<Triangle> triangles_;
    std::vector<float> planes_;
    float delta_{};
    bool sorted_{};
    float eps_{};

    std::vector<std::vector<std::pair<glm::vec3, glm::vec3>>> segments_;
    std::vector<std::vector<Polygon>> plane_polygons_;
};

inline std::map<int, std::vector<std::vector<glm::vec3>>> LoadModelAndMakeSlices(const std::string& filepath, const glm::vec3& normal, float heightstep = 0.1f) {
    std::vector<Triangle> triangles = LoadTrianglesFromFile(filepath, normal);
    Slicer slicer(triangles, {}, heightstep, true);
    slicer.incremental_slicing();
    auto& plane_polygons = slicer.planes_with_polygons();

    std::map<int, std::vector<std::vector<glm::vec3>>> result;
    for (std::size_t i = 0; i < plane_polygons.size(); ++i) {
        for (const auto& poly : plane_polygons[i]) {

            auto transformback_points = transformPointsBasisBack(poly.vertices, normal);
            
            result[static_cast<int>(i)].push_back(transformback_points);
        }
    }
    return result;
};

inline std::vector<std::vector<ContourPoint>> mergeLineSegments(const std::vector<std::vector<ContourPoint>>& linesegments)
{
    //use ContourConstrution to merge line segments into contours
    std::vector<std::vector<glm::vec3>> segs;
	// 保存ContourPoint的映射，用于后续恢复
	std::map<std::pair<int, int>, ContourPoint> pointMap;

    for (const auto& lineseg : linesegments)
    {
        for (int i = 0; i < lineseg.size() - 1; i++)
        {
            auto& seg = lineseg[i];
            auto& nextseg = lineseg[i + 1];
            segs.push_back({ glm::vec3(seg.Position), glm::vec3(nextseg.Position) });

            // 存储映射关系
            pointMap[{ static_cast<int>(seg.Position.x * 10000), static_cast<int>(seg.Position.y * 10000) }] = seg;
            pointMap[{ static_cast<int>(nextseg.Position.x * 10000), static_cast<int>(nextseg.Position.y * 10000) }] = nextseg;
        }
    }

    float eps = 1.0f/256.f; // 设置一个合适的精度值
    auto mergedContours = Slicer::ContourConstrution(segs, eps);

    // 恢复为ContourPoint结构
    std::vector<std::vector<ContourPoint>> result;
    for (const auto& contour : mergedContours)
    {
        std::vector<ContourPoint> contourPoints;
        for (const auto& pt : contour)
        {
            auto key = std::make_pair(static_cast<int>(pt.x * 10000), static_cast<int>(pt.y * 10000));
            if (pointMap.find(key) != pointMap.end())
            {
                contourPoints.push_back(pointMap[key]);
            }
            else
            {
                // 如果找不到对应的点，可以选择创建一个新的ContourPoint，或者跳过
                ContourPoint cp;
                cp.Position = glm::vec4(pt, 0.0f);
                cp.Normal = glm::vec3(0.0f, 0.0f, 1.0f); // 默认法线
                contourPoints.push_back(cp);
            }
        }
        result.push_back(contourPoints);
    }

    return result;
}
    
} // namespace slicing
