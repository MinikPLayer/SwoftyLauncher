#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The !pkg container of the game's UI databases (tools/pkg.py): read it into a tree, append a named entry to an
// array of entries, and write it back. A tree that is read and written without a change gives the same bytes.

namespace swofty_core::apply::content::uidb {

struct Val {
    std::string type, value;
    int64_t table = -1;
    uint32_t off = 0;
};

struct Node;

struct Node {
    enum Kind { value, object, array, iar } kind = value;
    Val val;
    std::vector<std::pair<Val, Node>> items;
    std::vector<Node> list;
    std::string tag;
    uint32_t version = 1;
    std::string raw;
};

struct Data {
    std::string type;
    uint32_t bpd = 0;
    std::string raw;
    std::map<std::string, uint32_t> index;
    uint32_t count() const { return bpd ? uint32_t(raw.size() / bpd) : 0; }
};

inline uint32_t fnv1a32(const std::string &s)
{
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s)
        h = (h ^ c) * 0x01000193u;
    return h;
}

inline uint32_t align_of(const std::string &t)
{
    if (t == "ui64" || t == "si64")
        return 8;
    if (t == "vec3" || t == "vec4" || t == "quat" || t == "woid" || t == "shnm" || t == "mat4")
        return 16;
    if (t == "bbox")
        return 32;
    return 4;
}

class Pkg {
public:
    std::string name = std::string(4, '\0');
    Node root;
    std::vector<Data> tables;
    std::string why;

    bool read(const std::string &d)
    {
        d_ = &d;
        if (d.size() < 16 || d.compare(0, 4, "!pkg"))
            return why = "not a !pkg", false;
        name = d.substr(4, 4);
        uint32_t a = u32(8), b = u32(12), vo = b & 0xFFFFFF;
        if (!inside(vo, 8) || d.compare(vo, 4, "!vca"))
            return why = "no !vca table", false;
        uint32_t n = u32(vo + 4);
        for (uint32_t i = 0; i < n; i++) {
            if (!inside(vo + 8 + 4 * i, 4))
                return why = "!vca table is cut off", false;
            uint32_t o = u32(vo + 8 + 4 * i) & 0xFFFFFF;
            if (!inside(o, 8))
                return why = "a data table is cut off", false;
            Data t;
            if (d.compare(o, 4, "!sbi") == 0) {
                uint32_t ln = u32(o + 4);
                if (!inside(o + 8, ln))
                    return why = "a string table is cut off", false;
                t.type = "stri";
                t.raw = d.substr(o + 8, ln);
                for (uint32_t p = 0; p < ln;) {
                    uint32_t len = uint8_t(t.raw[p]);
                    if (p + 1 + len > ln)
                        break;
                    t.index.emplace(t.raw.substr(p + 1, len), p);
                    p += len + 2;
                }
            } else if (d.compare(o, 4, "!vbi") == 0) {
                t.type = d.substr(o + 4, 4);
                uint32_t cnt = u32(o + 8);
                t.bpd = u32(o + 12);
                if (!inside(o + 16, size_t(cnt) * t.bpd))
                    return why = "a value table is cut off", false;
                t.raw = d.substr(o + 16, size_t(cnt) * t.bpd);
            } else
                return why = "unknown data table", false;
            tables.push_back(std::move(t));
        }
        bool ok = chunk(a & 0xFFFFFF, root);
        d_ = nullptr;
        return ok;
    }

    std::string write()
    {
        std::string out = "!pkg" + name + std::string(8, '\0');
        struct Slot {
            size_t at;
            const Node *node;
        };
        std::deque<Slot> q;
        auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
        auto add32 = [&](uint32_t v) { out.append(reinterpret_cast<const char *>(&v), 4); };
        auto emit = [&](const Node &node) -> size_t {
            size_t start = out.size();
            if (node.kind == Node::object) {
                size_t es = node.version == 1 ? 12 : 8;
                out += "!idi";
                add32(uint32_t(node.items.size()) | (node.version << 24));
                size_t table = out.size();
                out.append(es * node.items.size(), '\0');
                std::vector<const std::pair<Val, Node> *> order;
                for (auto &it : node.items)
                    order.push_back(&it);
                if (node.version == 1)
                    std::stable_sort(order.begin(), order.end(), [](auto *x, auto *y) { return fnv1a32(x->first.value) < fnv1a32(y->first.value); });
                for (size_t i = 0; i < order.size(); i++) {
                    put32(table + es * i, intern(order[i]->first));
                    if (node.version == 1)
                        put32(table + es * i + 8, fnv1a32(order[i]->first.value));
                    if (order[i]->second.kind == Node::value)
                        put32(table + es * i + 4, intern(order[i]->second.val));
                    else
                        q.push_back({table + es * i + 4, &order[i]->second});
                }
            } else if (node.kind == Node::array) {
                out += node.tag;
                add32(uint32_t(node.list.size()));
                size_t table = out.size();
                out.append(4 * node.list.size(), '\0');
                for (size_t i = 0; i < node.list.size(); i++) {
                    if (node.list[i].kind == Node::value)
                        put32(table + 4 * i, intern(node.list[i].val));
                    else
                        q.push_back({table + 4 * i, &node.list[i]});
                }
            } else if (node.kind == Node::iar) {
                out += "!iar" + node.raw;
            }
            return start;
        };
        size_t root_off = emit(root);
        while (!q.empty()) {
            Slot s = q.front();
            q.pop_front();
            put32(s.at, 0x80000000u | uint32_t(emit(*s.node)));
        }
        while (out.size() % 4)
            out += '\0';
        size_t vca = out.size();
        out += "!vca";
        add32(uint32_t(tables.size()));
        size_t table = out.size();
        out.append(4 * tables.size(), '\0');
        for (size_t i = 0; i < tables.size(); i++) {
            auto &dt = tables[i];
            while (out.size() % align_of(dt.type))
                out += '\0';
            put32(table + 4 * i, 0x80000000u | uint32_t(out.size()));
            if (dt.type == "stri") {
                out += "!sbi";
                add32(uint32_t(dt.raw.size()));
                out += dt.raw;
            } else {
                out += "!vbi" + dt.type;
                add32(dt.count());
                add32(dt.bpd);
                out += dt.raw;
            }
        }
        while (out.size() % 4)
            out += '\0';
        put32(8, 0x80000000u | uint32_t(root_off));
        put32(12, 0x80000000u | uint32_t(vca));
        return out;
    }

    Node *child(Node &obj, const std::string &key)
    {
        if (obj.kind != Node::object)
            return nullptr;
        for (auto &it : obj.items)
            if (it.first.value == key)
                return &it.second;
        return nullptr;
    }

    Node *find(const std::string &path)
    {
        Node *n = &root;
        size_t at = 0;
        while (n && at <= path.size()) {
            size_t end = path.find('.', at);
            if (end == std::string::npos)
                end = path.size();
            n = child(*n, path.substr(at, end - at));
            at = end + 1;
        }
        return n;
    }

    static std::string text_of(const Node &entry, const std::string &key)
    {
        for (auto &it : entry.items)
            if (it.first.value == key && it.second.kind == Node::value)
                return it.second.val.value;
        return {};
    }

    bool append_entry(Node &list, const std::string &name, const std::string &value, std::string &err)
    {
        if (list.kind != Node::array || list.list.empty() || list.list.back().kind != Node::object)
            return err = "not a list of entries", false;
        Node copy = list.list.back();
        bool named = false, valued = false;
        for (auto &it : copy.items) {
            if (it.first.value == "Name" && it.second.kind == Node::value)
                it.second.val = Val{"stri", name}, named = true;
            else if (it.first.value == "Value" && it.second.kind == Node::value)
                it.second.val = Val{"stri", value}, valued = true;
        }
        if (!named || !valued)
            return err = "the last entry has no Name or Value", false;
        list.list.push_back(std::move(copy));
        return true;
    }

private:
    const std::string *d_ = nullptr;

    uint32_t u32(size_t o) const
    {
        uint32_t v = 0;
        std::memcpy(&v, d_->data() + o, 4);
        return v;
    }
    bool inside(size_t o, size_t n) const { return o <= d_->size() && n <= d_->size() - o; }

    bool value_at(uint32_t ot, Node &out)
    {
        uint32_t t = ot >> 24, off = ot & 0xFFFFFF;
        if (t == 0x80)
            return chunk(off, out);
        if (t >= tables.size())
            return why = "a value names table " + std::to_string(t), false;
        Data &dt = tables[t];
        out.kind = Node::value;
        out.val.type = dt.type;
        out.val.table = t;
        out.val.off = off;
        if (dt.type == "stri") {
            if (off >= dt.raw.size() || off + 1u + uint8_t(dt.raw[off]) > dt.raw.size())
                return why = "a string is cut off", false;
            out.val.value = dt.raw.substr(off + 1, uint8_t(dt.raw[off]));
        } else {
            if (size_t(off + 1) * dt.bpd > dt.raw.size())
                return why = "a value is cut off", false;
            out.val.value = dt.raw.substr(size_t(off) * dt.bpd, dt.bpd);
        }
        return true;
    }

    bool chunk(uint32_t o, Node &out)
    {
        if (!inside(o, 8))
            return why = "a chunk is cut off", false;
        const std::string &d = *d_;
        if (d.compare(o, 4, "!idi") == 0) {
            uint32_t h = u32(o + 4), cnt = h & 0xFFFFFF;
            out.kind = Node::object;
            out.version = h >> 24;
            size_t es = out.version == 1 ? 12 : 8;
            if (!inside(o + 8, es * cnt))
                return why = "an object is cut off", false;
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t k = u32(o + 8 + es * i), v = u32(o + 8 + es * i + 4);
                Node kn, vn;
                if (!value_at(k, kn) || !value_at(v, vn))
                    return false;
                out.items.emplace_back(kn.val, std::move(vn));
            }
            return true;
        }
        if (d.compare(o, 4, "!ili") == 0 || d.compare(o, 4, "!vca") == 0) {
            uint32_t cnt = u32(o + 4);
            out.kind = Node::array;
            out.tag = d.substr(o, 4);
            if (!inside(o + 8, 4 * size_t(cnt)))
                return why = "an array is cut off", false;
            for (uint32_t i = 0; i < cnt; i++) {
                Node n;
                if (!value_at(u32(o + 8 + 4 * i), n))
                    return false;
                out.list.push_back(std::move(n));
            }
            return true;
        }
        if (d.compare(o, 4, "!iar") == 0) {
            if (!inside(o + 4, 8))
                return why = "an !iar is cut off", false;
            out.kind = Node::iar;
            out.raw = d.substr(o + 4, 8);
            return true;
        }
        return why = "unknown chunk", false;
    }

    bool same(const Data &dt, const Val &v) const
    {
        if (dt.type == "stri")
            return v.off < dt.raw.size() && v.off + 1u + uint8_t(dt.raw[v.off]) <= dt.raw.size() &&
                   dt.raw.compare(v.off + 1, uint8_t(dt.raw[v.off]), v.value) == 0 && uint8_t(dt.raw[v.off]) == v.value.size();
        return size_t(v.off + 1) * dt.bpd <= dt.raw.size() && dt.raw.compare(size_t(v.off) * dt.bpd, dt.bpd, v.value) == 0;
    }

    uint32_t intern(const Val &v)
    {
        for (size_t t = 0; t < tables.size(); t++)
            if (tables[t].type == v.type && v.table == int64_t(t) && same(tables[t], v))
                return (uint32_t(t) << 24) | v.off;
        size_t t = 0;
        for (; t < tables.size(); t++)
            if (tables[t].type == v.type)
                break;
        if (t == tables.size() || v.type != "stri")
            return 0xFFFFFFFFu;
        Data &dt = tables[t];
        auto hit = dt.index.find(v.value);
        if (hit != dt.index.end())
            return (uint32_t(t) << 24) | hit->second;
        uint32_t o = uint32_t(dt.raw.size());
        dt.raw += char(v.value.size()) + v.value + '\0';
        dt.index[v.value] = o;
        return (uint32_t(t) << 24) | o;
    }
};

}
