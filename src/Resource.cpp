#include "Resource.h"
//interface for IResource
#include "ResourceTexture.h"
#include "ResourceMesh.h"
#include "pystring.h"
#include "Log.h"

std::shared_ptr<IResource> ResourceManager::GetResource(const std::string& path) {
    // 返回基类指针版本
    if (pystring::endswith(path,".png"))
        return Get<ResourceTexture>(path);
    if(pystring::endswith(path, ".obj"))
        return Get<ResourceMesh>(path);
   
    ERROR("loading failed at {0}", path);
    throw std::string("not implement yet for this type");
}