#include "wayland/wire.h"

#include <cstring>

namespace pxr {
namespace wl {

namespace {
inline size_t pad4(size_t n) { return (n + 3) & ~static_cast<size_t>(3); }
}  // namespace

bool parseArgs(const char* sig, const uint8_t* p, size_t n, std::deque<int>& fds, Args& out) {
    size_t pos = 0;
    auto u32 = [&](uint32_t* v) {
        if (pos + 4 > n) return false;
        memcpy(v, p + pos, 4);
        pos += 4;
        return true;
    };
    auto str = [&](std::string* s, bool* isNull) {
        uint32_t len;
        if (!u32(&len)) return false;
        if (len == 0) {
            *isNull = true;
            return true;
        }
        if (pos + len > n) return false;
        s->assign(reinterpret_cast<const char*>(p + pos), len - 1);
        pos += pad4(len);
        return pos <= n;
    };
    for (const char* c = sig; *c; ++c) {
        if (*c == '?') {
            ++c;
            if (!*c) return false;
        }
        Value v;
        v.t = *c;
        switch (*c) {
            case 'i': {
                uint32_t x;
                if (!u32(&x)) return false;
                v.i = static_cast<int32_t>(x);
                break;
            }
            case 'u':
            case 'n': {
                if (!u32(&v.u)) return false;
                break;
            }
            case 'o': {
                if (!u32(&v.u)) return false;
                v.isNull = v.u == 0;
                break;
            }
            case 'f': {
                uint32_t x;
                if (!u32(&x)) return false;
                v.f = static_cast<int32_t>(x) / 256.0;
                break;
            }
            case 's': {
                if (!str(&v.s, &v.isNull)) return false;
                break;
            }
            case 'a': {
                uint32_t len;
                if (!u32(&len) || pos + len > n) return false;
                v.a.assign(p + pos, p + pos + len);
                pos += pad4(len);
                break;
            }
            case 'h': {
                if (fds.empty()) return false;
                v.fd = fds.front();
                fds.pop_front();
                break;
            }
            case 'N': {
                bool nul = false;
                if (!str(&v.s, &nul) || nul) return false;
                if (!u32(&v.version) || !u32(&v.u)) return false;
                break;
            }
            default:
                return false;
        }
        out.push_back(std::move(v));
    }
    return pos == n;
}

void encodeArgs(const char* sig, const Args& in, std::vector<uint8_t>& out, std::vector<int>& fdsOut) {
    auto put32 = [&](uint32_t v) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), b, b + 4);
    };
    size_t idx = 0;
    for (const char* c = sig; *c; ++c) {
        if (*c == '?') {
            ++c;
            if (!*c) return;
        }
        if (idx >= in.size()) return;
        const Value& v = in[idx++];
        switch (*c) {
            case 'i': put32(static_cast<uint32_t>(v.i)); break;
            case 'u':
            case 'o':
            case 'n': put32(v.u); break;
            case 'f': put32(static_cast<uint32_t>(static_cast<int32_t>(v.f * 256.0))); break;
            case 's': {
                if (v.isNull) {
                    put32(0);
                    break;
                }
                const uint32_t len = static_cast<uint32_t>(v.s.size() + 1);
                put32(len);
                out.insert(out.end(), v.s.begin(), v.s.end());
                out.push_back(0);
                out.resize(out.size() + (pad4(len) - len), 0);
                break;
            }
            case 'a': {
                put32(static_cast<uint32_t>(v.a.size()));
                out.insert(out.end(), v.a.begin(), v.a.end());
                out.resize(out.size() + (pad4(v.a.size()) - v.a.size()), 0);
                break;
            }
            case 'h': fdsOut.push_back(v.fd); break;
            default: return;
        }
    }
}

}  // namespace wl
}  // namespace pxr
