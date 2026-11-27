// Native COFF definitions needed by freestanding MSVC-targeted guest objects.
extern "C" {
int _fltused = 0;
void free(void*);
}

// Clang's exception type descriptors refer directly to the type_info vftable.
// The deleting destructor uses the system UCRT. Shared VCRUNTIME140 handles
// type matching and unwind. The guest does not use typeid or dynamic_cast.
class type_info {
public:
    virtual ~type_info() noexcept = default;
    static void operator delete(void* p) noexcept { free(p); }

private:
    const char* undecorated_name = nullptr;
    char decorated_name[1]{};
};
static_assert(sizeof(type_info) == 3 * sizeof(void*));

inline void* operator new(decltype(sizeof(0)), void* storage) noexcept { return storage; }

// Emit the vftable without registering a global constructor or destructor.
type_info* EmitGuestTypeInfo(void* storage) { return new (storage) type_info; }
