
#ifndef THREAD_SAFE_CONTAINER_H
#define THREAD_SAFE_CONTAINER_H

#include <unordered_map>
#include <mutex>
#include <stdexcept>

template<typename keytype, typename valuetype>
class thread_safe_unordered_map
{
private:
    std::unordered_map<keytype, valuetype> container;
    std::mutex lock;
public:

    void set_value(keytype key, valuetype value)
    {
        std::scoped_lock(lock);
        container[key] = value;
    }
    void emplace(keytype key, valuetype value)
    {
        std::scoped_lock(lock);
        container.emplace(std::pair(key, value));
    }
    valuetype operator[](keytype key) const
    {
        std::scoped_lock(lock);
        auto ref = container.find(key);
        if (ref != container.end())
        {
            return ref->second;
        }
        throw std::out_of_range("value not found in map");
    }

    auto const find(keytype key) const
    {
        std::scoped_lock(lock);
        return container.find(key);
    }
    auto const end() const
    {
        std::scoped_lock(lock);
        return container.end();
    }

    bool const exists(keytype key) const
    {
        std::scoped_lock(lock);
        auto it = container.find(key);
        return it != container.end();
    }
    void erase(keytype key) 
    {
        std::scoped_lock(lock);
        container.erase(key);
    }

};

#endif //THREAD_SAFE_CONTAINER_H
