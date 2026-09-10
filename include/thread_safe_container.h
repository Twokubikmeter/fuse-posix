
#ifndef THREAD_SAFE_CONTAINER_H
#define THREAD_SAFE_CONTAINER_H

#include <unordered_map>

template<typename keytype, typename valuetype>
class thread_safe_unordered_map
{
private:
    std::unordered_map<keytype, valuetype> container;
public:

    void set_value(keytype key, valuetype value)
    {
        container[key] = value;
    }
    valuetype const operator[](keytype key) const
    {
        return container[key];
    }

    bool const exists(keytype key) const
    {
        auto it = container.find(key);
        return it != container.end();
    }
};

#endif //THREAD_SAFE_CONTAINER_H
