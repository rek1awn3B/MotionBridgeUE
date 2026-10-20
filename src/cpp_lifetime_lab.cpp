#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Tracer
{
    explicit Tracer(std::string inputName)
        : name(std::move(inputName))
    {
        std::cout << "construct: " << name << '\n';
    }

    Tracer(const Tracer& other)
        : name(other.name + "_copy")
    {
        std::cout << "copy: " << other.name << " -> " << name << '\n';
    }

    Tracer(Tracer&& other) noexcept
        : name(std::move(other.name))
    {
        std::cout << "move into: " << name << '\n';
    }

    ~Tracer()
    {
        std::cout << "destroy: " << name << '\n';
    }

    std::string name;
};

void ReadByConstReference(const Tracer& object)
{
    std::cout << "const reference reads: " << object.name << '\n';
}

void ModifyByReference(Tracer& object)
{
    object.name += "_modified";
}

void ReceiveByValue(Tracer object)
{
    std::cout << "value parameter owns: " << object.name << '\n';
}

void ReadThroughPointer(const Tracer* object)
{
    if (object == nullptr)
    {
        std::cout << "pointer is null\n";
        return;
    }
    std::cout << "pointer reads: " << object->name << '\n';
}

std::unique_ptr<Tracer> MakeOwnedObject()
{
    return std::make_unique<Tracer>("heap_object");
}

int main()
{
    std::cout << "--- stack object and parameter passing ---\n";
    {
        Tracer stackObject("stack_object");
        ReadByConstReference(stackObject); // 不发生拷贝，只读访问。
        ModifyByReference(stackObject);    // 不发生拷贝，可以修改原对象。
        ReceiveByValue(stackObject);       // 拷贝出一个独立的函数参数。
        ReadThroughPointer(&stackObject);  // 非拥有指针，并且允许为空。
        ReadThroughPointer(nullptr);
    }

    std::cout << "\n--- unique ownership and move ---\n";
    {
        std::unique_ptr<Tracer> owner = MakeOwnedObject();
        std::unique_ptr<Tracer> newOwner = std::move(owner);

        std::cout << "old owner is " << (owner ? "valid" : "empty") << '\n';
        std::cout << "new owner has: " << newOwner->name << '\n';
    }

    std::cout << "\n--- container owns its elements ---\n";
    {
        std::vector<Tracer> objects;
        objects.reserve(2);
        objects.emplace_back("first");
        objects.emplace_back("second");
    }

    std::cout << "\nAll resources were released by destructors.\n";
    return 0;
}
