#pragma once

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <typeinfo>
#include <stdexcept>
#include <vector>
#include <functional>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <spdlog/spdlog.h>

namespace BeatMate {

class ServiceLocator
{
public:
    ServiceLocator() = default;
    ~ServiceLocator() { shutdownAll(); }

    ServiceLocator(const ServiceLocator&) = delete;
    ServiceLocator& operator=(const ServiceLocator&) = delete;

    template<typename T>
    void registerSingleton(std::unique_ptr<T> instance)
    {
        auto key = std::type_index(typeid(T));
        auto name = typeid(T).name();

        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if (m_services.count(key)) {
            spdlog::warn("[ServiceLocator] Service already registered, ignoring duplicate: {}", name);
            return;
        }

        auto* rawPtr = instance.get();
        (void)rawPtr;
        m_services[key] = ServiceEntry{
            std::shared_ptr<void>(instance.release(), [](void* p) {
                delete static_cast<T*>(p);
            }),
            name
        };
        m_shutdownOrder.push_back(key);

        spdlog::debug("[ServiceLocator] Registered: {}", name);
    }

    template<typename T>
    T& get()
    {
        auto key = std::type_index(typeid(T));
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_services.find(key);
        if (it == m_services.end()) {
            throw std::runtime_error(
                std::string("Service not registered: ") + typeid(T).name());
        }
        return *static_cast<T*>(it->second.instance.get());
    }

    template<typename T>
    const T& get() const
    {
        auto key = std::type_index(typeid(T));
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_services.find(key);
        if (it == m_services.end()) {
            throw std::runtime_error(
                std::string("Service not registered: ") + typeid(T).name());
        }
        return *static_cast<const T*>(it->second.instance.get());
    }

    template<typename T>
    T* tryGet()
    {
        auto key = std::type_index(typeid(T));
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_services.find(key);
        if (it == m_services.end()) return nullptr;
        return static_cast<T*>(it->second.instance.get());
    }

    template<typename T>
    const T* tryGet() const
    {
        auto key = std::type_index(typeid(T));
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_services.find(key);
        if (it == m_services.end()) return nullptr;
        return static_cast<const T*>(it->second.instance.get());
    }

    template<typename T>
    bool has() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_services.count(std::type_index(typeid(T))) > 0;
    }

    void shutdownAll()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (auto it = m_shutdownOrder.rbegin(); it != m_shutdownOrder.rend(); ++it) {
            auto sit = m_services.find(*it);
            if (sit != m_services.end()) {
                spdlog::debug("[ServiceLocator] Destroying: {}", sit->second.name);
                sit->second.instance.reset();
            }
        }
        m_services.clear();
        m_shutdownOrder.clear();
    }

    size_t serviceCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_services.size();
    }

private:
    struct ServiceEntry {
        std::shared_ptr<void> instance;
        std::string name;
    };

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::type_index, ServiceEntry> m_services;
    std::vector<std::type_index> m_shutdownOrder;
};

} // namespace BeatMate
