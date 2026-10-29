// SPDX-License-Identifier: MIT
#pragma once
#if defined(_WIN32)
#include <windows.h>
#endif
#include <atomic>
#include <memory>
#include <swofty/record.hpp>
#include <swofty/selector.hpp>

// The registry (standard library 1.6): typed handles over the core's SwoftyRegistry table. A Registry<T>
// is a named, typed collection of records every addon in the profile shares. add, variant and provide
// put records under your own keys; layer changes anyone's records, in load order. Every call returns
// unavailable (or an empty value) on a core without the registry, so an addon built against this header
// also loads on a 1.5 core.
//
// SWOFTY_REGISTER blocks run once per game start, at the early stage, in load order. The file that
// expands SWOFTY_ADDON() must include this header for the DLL to export SwoftyAddonRegister.

namespace swofty {

namespace detail {

inline const SwoftyRegistry *registry_table()
{
    if (!api || api->version < 9 || !api->registry)
        return nullptr;
#if defined(_WIN32)
    static std::atomic<int> exported{-1};
    if (exported.load() < 0) {
        HMODULE core = GetModuleHandleW(L"swofty_core.dll");
        exported = core && GetProcAddress(core, "SwoftyCoreRegistry") ? 1 : 0;
    }
    if (!exported.load())
        return nullptr;
#endif
    return api->registry->available && api->registry->available() ? api->registry : nullptr;
}

struct register_block {
    void (*fn)();
    register_block *next;
    explicit register_block(void (*f)()) : fn(f), next(nullptr) { append(blocks(), this); }
    static register_block *&blocks()
    {
        static register_block *head = nullptr;
        return head;
    }
};

inline int run_register(const SwoftyApi *a)
{
    if (!a || a->version < 9)
        return 1;
    if (!api)
        api = a;
    for (register_block *b = register_block::blocks(); b; b = b->next)
        b->fn();
    return 1;
}

template <class T> constexpr uint32_t option_type()
{
    if constexpr (std::is_same_v<T, bool>)
        return SWOFTY_SETTING_TOGGLE;
    else if constexpr (std::is_arithmetic_v<T>)
        return SWOFTY_SETTING_SLIDER;
    else
        return SWOFTY_SETTING_CHOICE;
}

}

template <class T> class Registry;

template <class T> class Select {
public:
    Select except(std::vector<Ref<T>> ids) const
    {
        Select s = *this;
        for (auto &r : ids)
            s.except_.push_back(std::string(r.id()));
        return s;
    }

    std::function<bool(const T &)> match_;
    std::string prefix_;
    std::string tag_;
    std::string tag_field_;
    std::vector<std::string> except_;
};

template <class T> Select<T> where(std::function<bool(const T &)> match)
{
    Select<T> s;
    s.match_ = std::move(match);
    return s;
}

template <class T> Select<T> under(std::string_view id_prefix)
{
    Select<T> s;
    s.prefix_ = id_prefix;
    return s;
}

template <class T> class Tag {
public:
    Tag() = default;
    Tag(std::string registry, std::string id) : registry_(std::move(registry)), id_(std::move(id)) {}
    std::string_view id() const { return id_; }
    std::string_view registry() const { return registry_; }
    bool contains(Ref<T> record) const
    {
        auto t = detail::registry_table();
        int in = 0;
        return t && t->tag_contains &&
               t->tag_contains(registry_.c_str(), id_.c_str(), std::string(record.id()).c_str(), &in) == SWOFTY_ASSET_OK &&
               in;
    }
    std::vector<Ref<T>> members() const
    {
        std::vector<Ref<T>> out;
        auto t = detail::registry_table();
        if (!t || !t->tag_members)
            return out;
        std::string buf;
        if (detail::fill(buf, [&](char *o, size_t cap, size_t *len) {
                return t->tag_members(registry_.c_str(), id_.c_str(), o, cap, len);
            }) == SWOFTY_ASSET_OK)
            from_json(buf, out);
        return out;
    }

private:
    std::string registry_, id_;
};

template <class T> Select<T> tagged(Tag<T> tag)
{
    Select<T> s;
    s.tag_ = tag.id();
    return s;
}

template <class T, class U> Select<T> tagged(Ref<U> T::*field, Tag<U> tag)
{
    Select<T> s;
    s.tag_ = tag.id();
    detail::each_field(
        [&](const auto &f) {
            if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(f)>, detail::field_end>) {
                if constexpr (std::is_same_v<decltype(f.member), Ref<U> T::*>)
                    if (f.member == field)
                        s.tag_field_ = std::string("/") + f.name;
            }
        },
        swofty_fields(static_cast<const T *>(nullptr)));
    return s;
}

template <class T> class LayerHandle {
public:
    LayerHandle() = default;
    explicit LayerHandle(uint32_t id, Result r) : id_(id), result_(r) {}
    LayerHandle &in(events::Selector where)
    {
        filters_.push_back(std::move(where));
        auto t = detail::registry_table();
        if (!t || !t->layer_in || !id_)
            result_ = Result::unavailable;
        else
            result_ = detail::result(t->layer_in(id_, detail::text(events::detail::array(filters_))));
        return *this;
    }
    Result result() const { return result_; }
    uint32_t id() const { return id_; }

private:
    uint32_t id_ = 0;
    Result result_ = Result::unavailable;
    std::vector<events::Selector> filters_;
};

struct SharedIds {
    const char *provider_registry = nullptr;
    const char *provides = "/provides";
    const char *resolved = "/team";
};

struct RegistryOptions {
    Applies applies = Applies::read;
    bool snapshot = false;
    std::size_t snapshot_budget = 65536;
    SharedIds shared{};
};

inline std::string last_error()
{
    auto t = detail::registry_table();
    if (!t || !t->last_error)
        return {};
    char buf[512] = {};
    t->last_error(buf, sizeof buf);
    return detail::cstr(buf, sizeof buf);
}

template <class V> V option(std::string_view id)
{
    auto t = detail::registry_table();
    SwoftySettingValue v{};
    if (!t || !t->option || t->option(std::string(id).c_str(), detail::option_type<V>(), &v) != SWOFTY_ASSET_OK)
        return V{};
    if constexpr (std::is_same_v<V, bool>)
        return v.toggle != 0;
    else if constexpr (std::is_integral_v<V>)
        return static_cast<V>(v.slider < 0 ? v.slider - 0.5 : v.slider + 0.5);
    else if constexpr (std::is_floating_point_v<V>)
        return static_cast<V>(v.slider);
    else
        return V(v.choice ? v.choice : "");
}

template <class T> class Registry {
public:
    using value_type = T;

    Registry(std::string_view name, RegistryOptions opts = {}) : name_(name), opts_(opts) {}
    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;

    std::string_view name() const { return name_; }
    const RegistryOptions &options() const { return opts_; }

    Ref<T> add(std::string_view key, T value)
    {
        auto t = table();
        if (!t || !t->add)
            return {};
        char id[256] = {};
        std::string v = encode(value);
        if (t->add(name_.c_str(), std::string(key).c_str(), detail::text(v), id, sizeof id) != SWOFTY_ASSET_OK)
            return {};
        return Ref<T>(detail::cstr(id, sizeof id));
    }

    Ref<T> variant(std::string_view key, Ref<T> base, Edit<T> edit)
    {
        auto t = table();
        if (!t || !t->variant)
            return {};
        char id[256] = {};
        auto *user = new Edit<T>(std::move(edit));
        if (t->variant(name_.c_str(), std::string(key).c_str(), std::string(base.id()).c_str(), &edit_entry, user, id,
                       sizeof id) != SWOFTY_ASSET_OK)
            return {};
        return Ref<T>(detail::cstr(id, sizeof id));
    }

    Result provide(std::function<std::optional<T>(std::string_view id)> fn,
                   std::function<void(std::function<void(std::string_view)>)> list = {})
    {
        auto t = table();
        if (!t || !t->provide_fn)
            return Result::unavailable;
        auto *p = new Provider{std::move(fn), std::move(list)};
        return detail::result(t->provide_fn(name_.c_str(), &provide_entry, p->list ? &list_entry : nullptr, p));
    }

    Result invalidate(Ref<T> id = {})
    {
        auto t = table();
        if (!t || !t->invalidate)
            return Result::unavailable;
        std::string s(id.id());
        return detail::result(t->invalidate(name_.c_str(), s.empty() ? nullptr : s.c_str()));
    }

    Result check(std::function<void(const T &, Errors &)> fn)
    {
        auto t = table();
        if (!t || !t->check)
            return Result::unavailable;
        auto *user = new std::function<void(const T &, Errors &)>(std::move(fn));
        return detail::result(t->check(name_.c_str(), &check_entry, user));
    }

    Result protect(Ref<T> own_record, std::vector<std::string_view> fields)
    {
        auto t = table();
        if (!t || !t->protect)
            return Result::unavailable;
        std::vector<std::string> names(fields.begin(), fields.end());
        std::vector<const char *> ptrs;
        for (auto &n : names)
            ptrs.push_back(n.c_str());
        return detail::result(
            t->protect(name_.c_str(), std::string(own_record.id()).c_str(), ptrs.data(), ptrs.size()));
    }

    LayerHandle<T> layer(Ref<T> target, Edit<T> edit, Scope scope = Scope::global)
    {
        SwoftyLayerDesc d = desc(scope);
        std::string id(target.id());
        d.id = id.c_str();
        return submit(d, std::move(edit));
    }

    LayerHandle<T> layer(Select<T> which, Edit<T> edit, Scope scope = Scope::global)
    {
        SwoftyLayerDesc d = desc(scope);
        std::vector<const char *> except;
        for (auto &e : which.except_)
            except.push_back(e.c_str());
        auto *match = which.match_ ? new std::function<bool(const T &)>(std::move(which.match_)) : nullptr;
        d.match = match ? &match_entry : nullptr;
        d.match_user = match;
        d.prefix = which.prefix_.empty() ? nullptr : which.prefix_.c_str();
        d.except = except.data();
        d.except_count = except.size();
        d.tag = which.tag_.empty() ? nullptr : which.tag_.c_str();
        d.tag_field = which.tag_field_.empty() ? nullptr : which.tag_field_.c_str();
        return submit(d, std::move(edit));
    }

    Result clear_layer(Ref<T> target, Scope scope = Scope::global)
    {
        auto t = table();
        if (!t || !t->clear_layer)
            return Result::unavailable;
        return detail::result(
            t->clear_layer(name_.c_str(), std::string(target.id()).c_str(), static_cast<uint32_t>(scope)));
    }

    Tag<T> tag(std::string_view id) const { return Tag<T>(name_, std::string(id)); }

    Result tag_add(Tag<T> tag, Ref<T> member)
    {
        auto t = table();
        if (!t || !t->tag_add)
            return Result::unavailable;
        return detail::result(
            t->tag_add(name_.c_str(), std::string(tag.id()).c_str(), std::string(member.id()).c_str()));
    }

    Ref<T> operator[](std::string_view id) const { return Ref<T>(id); }

    std::optional<T> get(Ref<T> id) const
    {
        auto t = table();
        if (!t || !t->get || id.empty())
            return std::nullopt;
        std::string at = at_major(), rid(id.id()), buf;
        if (detail::fill(buf, [&](char *o, size_t cap, size_t *len) { return t->get(at.c_str(), rid.c_str(), o, cap, len); }) !=
            SWOFTY_ASSET_OK)
            return std::nullopt;
        T value{};
        if constexpr (std::is_same_v<T, json::Value>)
            value = json::parse(buf);
        else if (!from_json(buf, value))
            return std::nullopt;
        return value;
    }

    Status status(Ref<T> id) const
    {
        auto t = table();
        if (!t || !t->status)
            return Status::no_registry;
        int s = t->status(name_.c_str(), std::string(id.id()).c_str());
        return s >= SWOFTY_STATUS_OK && s <= SWOFTY_STATUS_LATE ? static_cast<Status>(s) : Status::invalid;
    }

    std::vector<Ref<T>> all(std::string_view prefix = {}) const
    {
        std::vector<Ref<T>> out;
        auto t = table();
        if (!t || !t->first || !t->next)
            return out;
        std::string p(prefix);
        char id[256] = {};
        if (t->first(name_.c_str(), p.c_str(), id, sizeof id) != SWOFTY_ASSET_OK)
            return out;
        for (;;) {
            out.emplace_back(detail::cstr(id, sizeof id));
            std::string after(out.back().id());
            std::memset(id, 0, sizeof id);
            if (t->next(name_.c_str(), p.c_str(), after.c_str(), id, sizeof id) != SWOFTY_ASSET_OK)
                break;
        }
        return out;
    }

    std::string origin(Ref<T> id, std::string_view field) const
    {
        auto t = table();
        if (!t || !t->origin)
            return {};
        std::string pointer = field.empty() || field[0] == '/' ? std::string(field) : "/" + std::string(field);
        char addon[128] = {};
        if (t->origin(name_.c_str(), std::string(id.id()).c_str(), pointer.c_str(), addon, sizeof addon) != SWOFTY_ASSET_OK)
            return {};
        return detail::cstr(addon, sizeof addon);
    }

    Token watch(std::function<void(Ref<T>)> fn)
    {
        auto t = table();
        if (!t || !t->watch)
            return {};
        auto *user = new std::function<void(Ref<T>)>(std::move(fn));
        uint32_t token = 0;
        if (t->watch(name_.c_str(), &watch_entry, user, &token) != SWOFTY_ASSET_OK)
            return {};
        return Token(token, t->unwatch);
    }

    std::optional<int64_t> stat_id(Ref<T> id) const
    {
        auto t = table();
        int64_t v = -1;
        if (!t || !t->stat_id || t->stat_id(name_.c_str(), std::string(id.id()).c_str(), &v) != SWOFTY_ASSET_OK || v < 0)
            return std::nullopt;
        return v;
    }

    std::string at_major() const
    {
        if constexpr (requires { T::version; })
            return name_ + "@" + std::to_string(T::version.major);
        else
            return name_;
    }

    std::string schema() const
    {
        if constexpr (detail::record<T>) {
            detail::Writer w;
            detail::schema<T>(w);
            std::string body = w.out;
            std::string head = "{\"x-registry\":" + events::detail::quote(name_);
            if constexpr (requires { T::version; })
                head += ",\"x-version\":\"" + std::to_string(T::version.major) + "." + std::to_string(T::version.minor) + "\"";
            head += ",\"x-applies\":\"" + std::string(swofty_enum_name(opts_.applies)) + "\"";
            head += std::string(",\"x-snapshot\":") + (opts_.snapshot ? "true" : "false");
            if (opts_.snapshot)
                head += ",\"x-snapshot-budget\":" + std::to_string(opts_.snapshot_budget);
            if (opts_.shared.provider_registry)
                head += ",\"x-shared\":{\"provider_registry\":" + events::detail::quote(opts_.shared.provider_registry) +
                        ",\"provides\":" + events::detail::quote(opts_.shared.provides) +
                        ",\"resolved\":" + events::detail::quote(opts_.shared.resolved) + "}";
            return head + "," + body.substr(1);
        } else
            return {};
    }

private:
    struct Provider {
        std::function<std::optional<T>(std::string_view)> fn;
        std::function<void(std::function<void(std::string_view)>)> list;
    };

    const SwoftyRegistry *table() const
    {
        auto t = detail::registry_table();
        if (t && !declared_.exchange(true) && t->declare) {
            std::string s = schema();
            if (!s.empty())
                t->declare(name_.c_str(), detail::text(s));
        }
        return t;
    }

    static std::string encode(const T &value)
    {
        if constexpr (std::is_same_v<T, json::Value>)
            return json::dump(value);
        else
            return to_json(value);
    }

    SwoftyLayerDesc desc(Scope scope) const
    {
        SwoftyLayerDesc d{};
        d.size = sizeof d;
        d.scope = static_cast<uint32_t>(scope);
        d.registry = name_.c_str();
        return d;
    }

    LayerHandle<T> submit(SwoftyLayerDesc &d, Edit<T> edit)
    {
        auto t = table();
        if (!t || !t->layer_ex)
            return LayerHandle<T>(0, Result::unavailable);
        auto *user = new Edit<T>(std::move(edit));
        d.edit = &edit_entry;
        d.user = user;
        uint32_t id = 0;
        Result r = detail::result(t->layer_ex(&d, &id));
        return LayerHandle<T>(id, r);
    }

    static int edit_entry(void *user, SwoftyText in, char *out, size_t cap, size_t *len)
    {
        return detail::guarded_entry([&] { return edit_body(user, in, out, cap, len); });
    }

    static int edit_body(void *user, SwoftyText in, char *out, size_t cap, size_t *len)
    {
        const auto &edit = *static_cast<Edit<T> *>(user);
        T value{};
        if constexpr (std::is_same_v<T, json::Value>)
            value = json::parse(std::string_view(in.data, in.size));
        else if (!from_json(std::string_view(in.data, in.size), value))
            return SWOFTY_ASSET_INVALID;
        edit(value);
        return detail::put(encode(value), out, cap, len);
    }

    static int match_entry(void *user, SwoftyText base)
    {
        return detail::guarded_entry([&] { return match_body(user, base); });
    }

    static int match_body(void *user, SwoftyText base)
    {
        const auto &match = *static_cast<std::function<bool(const T &)> *>(user);
        T value{};
        if constexpr (std::is_same_v<T, json::Value>)
            value = json::parse(std::string_view(base.data, base.size));
        else if (!from_json(std::string_view(base.data, base.size), value))
            return 0;
        return match(value) ? 1 : 0;
    }

    static int provide_entry(void *user, const char *registry, const char *id, char *out, size_t cap, size_t *len)
    {
        return detail::guarded_entry([&] { return provide_body(user, registry, id, out, cap, len); });
    }

    static int provide_body(void *user, const char *, const char *id, char *out, size_t cap, size_t *len)
    {
        auto *p = static_cast<Provider *>(user);
        std::optional<T> v = p->fn(id ? id : "");
        if (!v)
            return SWOFTY_ASSET_INVALID;
        return detail::put(encode(*v), out, cap, len);
    }

    static void list_entry(void *user, const char *, void (*add)(void *ctx, const char *id), void *ctx)
    {
        detail::guarded_entry([&] {
            auto *p = static_cast<Provider *>(user);
            p->list([&](std::string_view id) { add(ctx, std::string(id).c_str()); });
            return 0;
        });
    }

    static void check_entry(void *user, SwoftyText value, void (*error)(void *ctx, const char *path, const char *msg),
                            void *ctx)
    {
        detail::guarded_entry([&] {
            check_body(user, value, error, ctx);
            return 0;
        });
    }

    static void check_body(void *user, SwoftyText value, void (*error)(void *ctx, const char *path, const char *msg),
                           void *ctx)
    {
        const auto &fn = *static_cast<std::function<void(const T &, Errors &)> *>(user);
        T v{};
        if constexpr (std::is_same_v<T, json::Value>)
            v = json::parse(std::string_view(value.data, value.size));
        else if (!from_json(std::string_view(value.data, value.size), v)) {
            error(ctx, "", "the value does not match the record's fields");
            return;
        }
        Errors errors;
        fn(v, errors);
        for (const auto &[path, msg] : errors.all())
            error(ctx, path.c_str(), msg.c_str());
    }

    static void watch_entry(void *user, const char *, const char *id)
    {
        detail::guarded_entry([&] {
            if (id && *id)
                (*static_cast<std::function<void(Ref<T>)> *>(user))(Ref<T>(id));
            return 0;
        });
    }

    std::string name_;
    RegistryOptions opts_;
    mutable std::atomic<bool> declared_{false};
};

template <class T> bool Ref<T>::resolved() const
{
    auto t = detail::registry_table();
    const char *name = detail::registry_name<T>();
    return t && t->status && *name && !id_.empty() && t->status(name, id_.c_str()) == SWOFTY_STATUS_OK;
}

namespace detail {

template <class A, class B> struct Convert {
    std::function<B(A)> fn;
    static int entry(void *user, SwoftyText in, char *out, size_t cap, size_t *len)
    {
        return guarded_entry([&] { return body(user, in, out, cap, len); });
    }
    static int body(void *user, SwoftyText in, char *out, size_t cap, size_t *len)
    {
        A a{};
        if (!from_json(std::string_view(in.data, in.size), a))
            return SWOFTY_ASSET_INVALID;
        return put(to_json(static_cast<Convert *>(user)->fn(std::move(a))), out, cap, len);
    }
};

template <class T> uint32_t major_of()
{
    if constexpr (requires { T::version; })
        return T::version.major;
    else
        return 1;
}

}

template <class A, class B>
Result adapter(Registry<A> &from, Registry<B> &to, std::function<B(A)> up, std::function<A(B)> down)
{
    auto t = detail::registry_table();
    if (!t || !t->adapter || from.name() != to.name())
        return t ? Result::invalid : Result::unavailable;
    auto *u = new detail::Convert<A, B>{std::move(up)};
    auto *d = new detail::Convert<B, A>{std::move(down)};
    struct Pair {
        detail::Convert<A, B> *up;
        detail::Convert<B, A> *down;
    };
    (void)d;
    return detail::result(t->adapter(std::string(from.name()).c_str(), detail::major_of<A>(), detail::major_of<B>(),
                                     &detail::Convert<A, B>::entry, &detail::Convert<B, A>::entry, u));
}

template <class A, class B> Result migrate(Registry<A> &from, Registry<B> &to, std::function<B(A)> fn)
{
    auto t = detail::registry_table();
    if (!t || !t->migrate || from.name() != to.name())
        return t ? Result::invalid : Result::unavailable;
    auto *c = new detail::Convert<A, B>{std::move(fn)};
    return detail::result(t->migrate(std::string(from.name()).c_str(), detail::major_of<A>(), detail::major_of<B>(),
                                     &detail::Convert<A, B>::entry, c));
}

}

#define SWOFTY_REGISTER                                                                                                \
    namespace {                                                                                                        \
    namespace SWOFTY_NS {                                                                                              \
    void handler();                                                                                                    \
    const ::swofty::detail::register_block reg{&handler};                                                              \
    }                                                                                                                  \
    }                                                                                                                  \
    void SWOFTY_NS::handler()

#undef SWOFTY_ADDON
#define SWOFTY_ADDON(...)                                                                                              \
    extern "C" __declspec(dllexport) const uint32_t SwoftyAddonApi = SWOFTY_API_VERSION;                               \
    extern "C" __declspec(dllexport) int SwoftyAddonRegister(const SwoftyApi *a)                                       \
    {                                                                                                                  \
        return ::swofty::detail::run_register(a);                                                                      \
    }                                                                                                                  \
    extern "C" __declspec(dllexport) int SwoftyAddonInit(const SwoftyApi *a)                                           \
    {                                                                                                                  \
        return ::swofty::detail::init(a, __VA_OPT__(&__VA_ARGS__, ) nullptr);                                          \
    }
