#pragma once

#include <Object3D.h>

#include <memory>
#include <vector>

class ObjectInspector
{
public:
    virtual ~ObjectInspector() = default;
    virtual bool CanInspect(const Object3D& object) const = 0;
    virtual void Draw(Object3D& object) = 0;
};

class ObjectInspectorRegistry
{
public:
    static void Register(std::unique_ptr<ObjectInspector> inspector);
    static void DrawInspector(Object3D& object);

private:
    static std::vector<std::unique_ptr<ObjectInspector>>& Inspectors();
    static void EnsureDefaultInspectors();
};
