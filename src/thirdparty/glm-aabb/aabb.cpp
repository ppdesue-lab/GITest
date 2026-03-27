#include "AABB.hpp"
#include <glm/gtx/component_wise.hpp>

namespace glm {

AABB::AABB()
{
  setNull();
}

AABB::AABB(const glm::vec3& center, glm::float_t radius)
{
  setNull();
  extend(center, radius);
}

AABB::AABB(const glm::vec3& p1, const glm::vec3& p2)
{
  setNull();
  extend(p1);
  extend(p2);
}

AABB::AABB(const AABB& aabb)
{
  setNull();
  extend(aabb);
}

AABB::~AABB()
{
}

void AABB::extend(glm::float_t val)
{
  if (!isNull())
  {
    mMin -= glm::vec3(val);
    mMax += glm::vec3(val);
  }
}

void AABB::extend(const glm::vec3& p)
{
  if (!isNull())
  {
    mMin = glm::min(p, mMin);
    mMax = glm::max(p, mMax);
  }
  else
  {
    mMin = p;
    mMax = p;
  }
}

void AABB::multiply(const glm::vec3& dir)
{
    mMin *= dir;
    mMax *= dir;

    auto p_max = glm::max(mMin, mMax);
    auto p_min = glm::min(mMin, mMax);

    mMin = p_min;// glm::min(p, mMin);
    mMax = p_max;// glm::max(p, mMax);
}

void AABB::extend(const glm::vec3& p, glm::float_t radius)
{
  glm::vec3 r(radius);
  if (!isNull())
  {
    mMin = glm::min(p - r, mMin);
    mMax = glm::max(p + r, mMax);
  }
  else
  {
    mMin = p - r;
    mMax = p + r;
  }
}

void AABB::extend(const AABB& aabb)
{
  if (!aabb.isNull())
  {
    extend(aabb.mMin);
    extend(aabb.mMax);
  }
}

bool AABB::contain(const glm::vec3& point) const
{
    if(point.x >= mMin.x && point.x <= mMax.x &&
        point.y >= mMin.y && point.y <= mMax.y &&
        point.z >= mMin.z && point.z <= mMax.z)
        return true;

    return false;
}

bool AABB::containXY(const glm::vec3& point) const
{
    if (point.x >= mMin.x && point.x <= mMax.x &&
        point.y >= mMin.y && point.y <= mMax.y )
        return true;

    return false;
}

void AABB::extendDisk(const glm::vec3& c, const glm::vec3& n, glm::float_t r)
{
  if (glm::length(n) < 1.e-12) { extend(c); return; }
  glm::vec3 norm = glm::normalize(n);
  glm::float_t x = sqrt(1 - norm.x) * r;
  glm::float_t y = sqrt(1 - norm.y) * r;
  glm::float_t z = sqrt(1 - norm.z) * r;
  extend(c + glm::vec3(x,y,z));
  extend(c - glm::vec3(x,y,z));
}

glm::vec3 AABB::getDiagonal() const
{
  if (!isNull())
    return mMax - mMin;
  else
    return glm::vec3(0);
}

glm::float_t AABB::getLongestEdge() const
{
  return glm::compMax(getDiagonal());
}

glm::float_t AABB::getShortestEdge() const
{
  return glm::compMin(getDiagonal());
}

glm::vec3 AABB::getCenter() const
{
  if (!isNull())
  {
    glm::vec3 d = getDiagonal();
    return mMin + (d * glm::float_t(0.5));
  }
  else
  {
    return glm::vec3(0.0);
  }
}

void AABB::translate(const glm::vec3& v)
{
  if (!isNull())
  {
    mMin += v;
    mMax += v;
  }
}

void AABB::scale(const glm::vec3& s, const glm::vec3& o)
{
  if (!isNull())
  {
    mMin -= o;
    mMax -= o;

    mMin *= s;
    mMax *= s;

    mMin += o;
    mMax += o;
  }
}

bool AABB::overlaps(const AABB& bb) const
{
  if (isNull() || bb.isNull())
    return false;

  if( bb.mMin.x > mMax.x || bb.mMax.x < mMin.x)
    return false;
  else if( bb.mMin.y > mMax.y || bb.mMax.y < mMin.y)
    return false;
  else if( bb.mMin.z > mMax.z || bb.mMax.z < mMin.z)
    return false;

  return true;
}

AABB AABB::overlappedBox(const AABB& bb) const
{
    AABB overlappedbox;
    if (!bb.isNull() && this->overlaps(bb))
    {
        auto min_pt = glm::min(this->mMax, bb.mMax);
        auto max_pt = glm::max(this->mMin, bb.mMin);
        overlappedbox.extend(min_pt);
		overlappedbox.extend(max_pt);
    }
    return overlappedbox;
}

AABB::INTERSECTION_TYPE AABB::intersect(const AABB& b) const
{
  if (isNull() || b.isNull())
    return OUTSIDE;
  float eps = 0.000001f;
  if ((mMax.x < b.mMin.x) || (mMin.x > b.mMax.x) ||
      (mMax.y < b.mMin.y) || (mMin.y > b.mMax.y) ||
      (mMax.z < b.mMin.z - eps) || (mMin.z - eps > b.mMax.z ))
  {
    return OUTSIDE;
  }

  if ((mMin.x <= b.mMin.x) && (mMax.x >= b.mMax.x) &&
      (mMin.y <= b.mMin.y) && (mMax.y >= b.mMax.y) &&
      (mMin.z - eps <= b.mMin.z) && (mMax.z >= b.mMax.z - eps))
  {
    return INSIDE;
  }

  return INTERSECT;    
}


bool AABB::isSimilarTo(const AABB& b, glm::float_t diff) const
{
  if (isNull() || b.isNull()) return false;

  glm::vec3 acceptable_diff=( (getDiagonal()+b.getDiagonal()) / glm::float_t(2.0))*diff;
  glm::vec3 min_diff(mMin-b.mMin);
  min_diff = glm::vec3(fabs(min_diff.x),fabs(min_diff.y),fabs(min_diff.z));
  if (min_diff.x > acceptable_diff.x) return false;
  if (min_diff.y > acceptable_diff.y) return false;
  if (min_diff.z > acceptable_diff.z) return false;
  glm::vec3 max_diff(mMax-b.mMax);
  max_diff = glm::vec3(fabs(max_diff.x),fabs(max_diff.y),fabs(max_diff.z));
  if (max_diff.x > acceptable_diff.x) return false;
  if (max_diff.y > acceptable_diff.y) return false;
  if (max_diff.z > acceptable_diff.z) return false;
  return true;
}

std::vector<glm::vec3> AABB::getCorners(const glm::vec3& offset) const
{
    std::vector < glm::vec3> pts;
    pts.push_back(glm::vec3(mMin.x, mMin.y, mMin.z)+offset);
    pts.push_back(glm::vec3(mMin.x, mMax.y, mMin.z)+offset);
    pts.push_back(glm::vec3(mMax.x, mMax.y, mMin.z)+offset);
    pts.push_back(glm::vec3(mMax.x, mMin.y, mMin.z)+offset);


    pts.push_back(glm::vec3(mMin.x, mMin.y, mMax.z)+offset);
    pts.push_back(glm::vec3(mMin.x, mMax.y, mMax.z)+offset);
    pts.push_back(glm::vec3(mMax.x, mMax.y, mMax.z)+offset);
    pts.push_back(glm::vec3(mMax.x, mMin.y, mMax.z)+offset);

    return pts;
}

} // namespace CPM_GLM_AABB_NS

