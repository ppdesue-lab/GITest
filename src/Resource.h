#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <queue>
#include <condition_variable>
#include <future>
#include <type_traits>
#include <filesystem>
#include <variant>
#include <optional>
#include <sstream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include "pystring.h"


// 资源状态枚举
enum class ResourceState : uint8_t {
    Unloaded = 0,      // 未加载
    Loading = 1,       // 加载中
    Loaded = 2,        // 已加载
    Failed = 3,        // 加载失败
    Unloading = 4      // 卸载中
};

// 资源类型枚举
enum class ResourceType : uint8_t {
    Unknown = 0,
    Texture = 1,
    Mesh = 2,
    Audio = 3,
    Material = 4,
    Shader = 5,
    Font = 6,
    Animation = 7,
    Script = 8,
    Prefab = 9,
    Level = 10
};

// 加载优先级
enum class LoadPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

// 资源元数据
struct ResourceMetadata {
    std::string path;
    std::string extension;
    ResourceType type;
    size_t sizeBytes;
    uint64_t lastModifiedTime;
    std::string checksum;
    
    ResourceMetadata() : type(ResourceType::Unknown), sizeBytes(0), lastModifiedTime(0) {}
};

// 加载请求
struct LoadRequest {
    std::string path;
    LoadPriority priority;
    std::function<void(class IResource*)> callback;
    uint64_t requestId;
    
    LoadRequest() : priority(LoadPriority::Normal), requestId(0) {}
    
    LoadRequest(const std::string& p, LoadPriority prio, uint64_t id)
        : path(p), priority(prio), requestId(id) {}

    bool operator < (const LoadRequest& b) const
    {
        return priority < b.priority;
    }
};


// ============================================
// 资源基类
// ============================================

class IResource {
public:
    virtual ~IResource() {
        if (m_state == ResourceState::Loaded) {
            Unload();
        }
    }

    // ==================== 核心接口 ====================
    
    // 加载资源（同步）
    virtual bool Load(const std::string& path) = 0;
    
    // 卸载资源
    virtual void Unload() {};
    
    // 重新加载资源
    virtual bool Reload() {
        if (m_state == ResourceState::Loaded) {
            Unload();
        }
        return Load(m_path);
    }

    // ==================== 状态查询 ====================
    
    ResourceState GetState() const { return m_state; }
    bool IsLoaded() const { return m_state == ResourceState::Loaded; }
    bool IsLoading() const { return m_state == ResourceState::Loading; }
    
    const std::string& GetPath() const { return m_path; }
    const std::string& GetName() const { return m_name; }
    
    const ResourceMetadata& GetMetadata() const { return m_metadata; }
    ResourceMetadata& GetMetadata() { return m_metadata; }
    
    size_t GetSizeBytes() const { return m_sizeBytes; }
    uint64_t GetLastUsedTime() const { return m_lastUsedTime; }
    
    // ==================== 引用计数扩展 ====================
    
    uint32_t GetRefCount() const { return m_refCount.load(std::memory_order_relaxed); }
    uint32_t IncrementRef() { return m_refCount.fetch_add(1, std::memory_order_acquire); }
    uint32_t DecrementRef() { return m_refCount.fetch_sub(1, std::memory_order_release); }
    
    // ==================== 资源类型识别 ====================
    
    virtual ResourceType GetType() const = 0;
    virtual std::string GetTypeName() const = 0;

protected:
    // ==================== 状态管理 ====================
    
    void SetState(ResourceState state) { 
        m_state = state; 
    }
    
    void SetPath(const std::string& path) { 
        m_path = path; 
        m_name = std::filesystem::path(path).stem().string();
        m_metadata.path = path;
        m_metadata.extension = std::filesystem::path(path).extension().string();
    }
    
    void SetSizeBytes(size_t size) { 
        m_sizeBytes = size;
        m_metadata.sizeBytes = size;
    }
    
    void UpdateLastUsedTime() { 
        m_lastUsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    // ==================== 受保护的成员变量 ====================
    
    std::string m_path;
    std::string m_name;
    ResourceMetadata m_metadata;
    std::atomic<ResourceState> m_state{ResourceState::Unloaded};
    std::atomic<size_t> m_sizeBytes{0};
    std::atomic<uint64_t> m_lastUsedTime{0};
    std::atomic<uint32_t> m_refCount{0};
    
protected:
    // 禁止直接构造
    IResource() = default;
    
    // 允许ResourceManager访问受保护成员
    friend class ResourceManager;
};

// ============================================
// 模板基类（简化资源实现）
// ============================================
#define TYPE_TO_STRING(x) #x

template<ResourceType T>
class Resource : public IResource {
public:
    ResourceType GetType() const override { return T; }
    std::string GetTypeName() const override { return TYPE_TO_STRING(T); }
    
    bool Load(const std::string& path) override { return false; };
protected:
    Resource() = default;
};

// ============================================
// 资源管理器
// ============================================

class ResourceManager {
public:
    // ==================== 单例访问 ====================
    
    static ResourceManager& Instance() {
        static ResourceManager instance;
        return instance;
    }
    
    // ==================== 禁止拷贝 ====================
    
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    // ==================== 初始化和销毁 ====================
    
    bool Initialize(const std::string& basePath = "") {
        std::lock_guard<std::mutex> lock(m_initializationMutex);
        
        if (m_initialized) {
            return true;
        }
        
        m_basePath = basePath;
        m_running = true;
        
        // 启动异步加载线程
        m_ioThread = std::thread(&ResourceManager::AsyncLoadingLoop, this);
        
        m_initialized = true;
        return true;
    }
    
    void Shutdown() {
        if (!m_initialized) return;
        
        m_running = false;
        
        // 通知所有等待的线程
        m_cv.notify_all();
        
        // 等待IO线程结束
        if (m_ioThread.joinable()) {
            m_ioThread.join();
        }
        
        // 卸载所有资源
        UnloadAllResources();
        
        m_initialized = false;
    }
    
    // ==================== 资源获取（同步） ====================
    
    template<typename T>
    std::shared_ptr<T> Get(const std::string& path) {
        static_assert(std::is_base_of<IResource, T>::value, 
                      "Template argument must derive from IResource");
        
        std::string normalizedPath = NormalizePath(path);
        
        // 尝试从缓存获取
        {
            std::shared_lock<std::shared_mutex> readLock(m_resourceMutex);
            auto it = m_resourceCache.find(normalizedPath);
            if (it != m_resourceCache.end()) {
                std::shared_ptr<IResource> baseResource = it->second.lock();
                if (baseResource && baseResource->GetState() == ResourceState::Loaded) {
                    // 更新最后使用时间
                    baseResource->UpdateLastUsedTime();
                    return std::dynamic_pointer_cast<T>(baseResource);
                }
            }
        }
        
        // 缓存未命中，需要加载
        std::unique_lock<std::shared_mutex> writeLock(m_resourceMutex);
        
        // 再次检查（双重检查锁定）
        auto it = m_resourceCache.find(normalizedPath);
        if (it != m_resourceCache.end()) {
            std::shared_ptr<IResource> baseResource = it->second.lock();
            if (baseResource && baseResource->GetState() == ResourceState::Loaded) {
                baseResource->UpdateLastUsedTime();
                return std::dynamic_pointer_cast<T>(baseResource);
            }
        }
        
        // 创建新资源
        std::shared_ptr<T> newResource = std::make_shared<T>();
        newResource->SetPath(normalizedPath);
        newResource->SetState(ResourceState::Loading);
        
        // 存入缓存（使用weak_ptr）
        m_resourceCache[normalizedPath] = newResource;
        
        // 释放写锁后执行加载
        writeLock.unlock();
        
        // 执行加载
        bool loadSuccess = newResource->Load(normalizedPath);
        newResource->SetState(loadSuccess ? ResourceState::Loaded : ResourceState::Failed);
        
        if (loadSuccess) {
            newResource->UpdateLastUsedTime();
        } else {
            // 加载失败，移除缓存
            std::unique_lock<std::shared_mutex> removeLock(m_resourceMutex);
            m_resourceCache.erase(normalizedPath);
        }
        
        return loadSuccess ? newResource : nullptr;
    }
    
    // ==================== 资源获取（异步） ====================
    
    template<typename T>
    std::future<std::shared_ptr<T>> LoadAsync(const std::string& path, 
                                               LoadPriority priority = LoadPriority::Normal) {
        return std::async(std::launch::async, [this, path]() {
            return this->Get<T>(path);
        });
    }
    
    // 带回调的异步加载
    uint64_t LoadAsyncWithCallback(const std::string& path, 
                                    std::function<void(std::shared_ptr<IResource>)> callback,
                                    LoadPriority priority = LoadPriority::Normal) {
        uint64_t requestId = ++m_requestIdCounter;
        
        std::lock_guard<std::mutex> lock(m_requestQueueMutex);
        m_loadQueue.push(LoadRequest(path, priority, requestId));
        m_pendingCallbacks[requestId] = callback;
        m_cv.notify_one();
        
        return requestId;
    }
    
    // ==================== 资源卸载 ====================
    
    bool Unload(const std::string& path) {
        std::string normalizedPath = NormalizePath(path);
        
        std::unique_lock<std::shared_mutex> lock(m_resourceMutex);
        auto it = m_resourceCache.find(normalizedPath);
        if (it != m_resourceCache.end()) {
            std::shared_ptr<IResource> resource = it->second.lock();
            if (resource) {
                resource->Unload();
            }
            m_resourceCache.erase(it);
            return true;
        }
        return false;
    }
    
    void UnloadUnusedResources() {
        std::unique_lock<std::shared_mutex> lock(m_resourceMutex);
        
        std::vector<std::string> toRemove;
        
        for (auto& pair : m_resourceCache) {
            std::shared_ptr<IResource> resource = pair.second.lock();
            if (!resource) {
                // 资源已被销毁，清理缓存条目
                toRemove.push_back(pair.first);
            } else if (resource->GetRefCount() == 0 && 
                       resource->GetState() == ResourceState::Loaded) {
                // 引用计数为0且已加载，可以卸载
                resource->Unload();
                resource->SetState(ResourceState::Unloaded);
                toRemove.push_back(pair.first);
            }
        }
        
        for (const auto& key : toRemove) {
            m_resourceCache.erase(key);
        }
    }
    
    void UnloadAllResources() {
        std::unique_lock<std::shared_mutex> lock(m_resourceMutex);
        
        for (auto& pair : m_resourceCache) {
            std::shared_ptr<IResource> resource = pair.second.lock();
            if (resource) {
                resource->Unload();
                resource->SetState(ResourceState::Unloaded);
            }
        }
        m_resourceCache.clear();
    }
    
    // ==================== 资源查询 ====================
    
    bool HasResource(const std::string& path) const {
        std::string normalizedPath = NormalizePath(path);
        std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
        return m_resourceCache.find(normalizedPath) != m_resourceCache.end();
    }
    
    std::shared_ptr<IResource> GetResource(const std::string& path);
    
    size_t GetLoadedResourceCount() const {
        std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
        size_t count = 0;
        for (const auto& pair : m_resourceCache) {
            std::shared_ptr<IResource> resource = pair.second.lock();
            if (resource && resource->GetState() == ResourceState::Loaded) {
                count++;
            }
        }
        return count;
    }
    
    size_t GetMemoryUsage() const {
        std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
        size_t totalSize = 0;
        for (const auto& pair : m_resourceCache) {
            std::shared_ptr<IResource> resource = pair.second.lock();
            if (resource && resource->GetState() == ResourceState::Loaded) {
                totalSize += resource->GetSizeBytes();
            }
        }
        return totalSize;
    }
    
    // ==================== 资源遍历 ====================
    
    template<typename Func>
    void ForEachResource(Func&& func) {
        std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
        for (const auto& pair : m_resourceCache) {
            std::shared_ptr<IResource> resource = pair.second.lock();
            if (resource) {
                func(resource);
            }
        }
    }
    
    // ==================== 配置和统计 ====================
    
    void SetBasePath(const std::string& path) {
        m_basePath = path;
    }
    
    const std::string& GetBasePath() const { return m_basePath; }
    
    struct Statistics {
        size_t loadedCount;
        size_t cachedCount;
        size_t memoryUsageBytes;
        size_t pendingAsyncLoads;
    };
    
    Statistics GetStatistics() {
        Statistics stats;
        stats.loadedCount = GetLoadedResourceCount();
        
        std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
        stats.cachedCount = m_resourceCache.size();
        stats.memoryUsageBytes = GetMemoryUsage();
        
        std::lock_guard<std::mutex> requestLock(m_requestQueueMutex);
        stats.pendingAsyncLoads = m_loadQueue.size();
        
        return stats;
    }

private:
    // ==================== 私有构造函数 ====================
    
    ResourceManager()
        : m_initialized(false), m_running(false), m_requestIdCounter(0) {}
    
    // ==================== 路径处理 ====================
    
    std::string NormalizePath(const std::string& path) const {
        if (m_basePath.empty()) {
            return path;
        }
        
        std::filesystem::path fullPath = std::filesystem::path(m_basePath) / path;
        return fullPath.string();
    }
    
    // ==================== 异步加载循环 ====================
    
    void AsyncLoadingLoop() {
        while (m_running) {
            LoadRequest request;
            
            {
                std::unique_lock<std::mutex> lock(m_requestQueueMutex);
                m_cv.wait(lock, [this]() {
                    return !m_running || !m_loadQueue.empty();
                });
                
                if (!m_running) break;
                
                if (m_loadQueue.empty()) continue;
                
                // 获取最高优先级的请求
                request = m_loadQueue.top();
                m_loadQueue.pop();
            }
            
            // 加载资源
            std::shared_ptr<IResource> resource = GetResource(request.path);
            
            // 执行回调
            if (request.callback) {
                std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
                auto it = m_pendingCallbacks.find(request.requestId);
                if (it != m_pendingCallbacks.end()) {
                    it->second(resource);
                    m_pendingCallbacks.erase(it);
                }
            }
        }
    }
    
    // ==================== 成员变量 ====================
    
    bool m_initialized;
    bool m_running;
    std::string m_basePath;
    
    // 资源缓存：路径 -> weak_ptr
    std::unordered_map<std::string, std::weak_ptr<IResource>> m_resourceCache;
    mutable std::shared_mutex m_resourceMutex;
    
    // 异步加载队列
    std::priority_queue<LoadRequest> m_loadQueue;
    std::mutex m_requestQueueMutex;
    std::condition_variable m_cv;
    std::thread m_ioThread;
    
    // 回调管理
    std::mutex m_callbackMutex;
    std::unordered_map<uint64_t, std::function<void(std::shared_ptr<IResource>)>> m_pendingCallbacks;
    
    // 请求ID计数器
    std::atomic<uint64_t> m_requestIdCounter;
    
    // 初始化互斥锁
    std::mutex m_initializationMutex;
};

// 便捷宏定义
#define RESOURCE_MANAGER ResourceManager::Instance()

