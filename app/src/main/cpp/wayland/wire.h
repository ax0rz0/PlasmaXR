// Wayland wire format: message descriptors (generated from the protocol XML) and argument
// marshalling. Everything is 32-bit words; file descriptors travel out of band (SCM_RIGHTS).
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace pxr {
namespace wl {

// One request or event. `sig` has one character per argument:
//   i int, u uint, f fixed, s string, o object, n new_id, a array, h fd, N untyped new_id,
//   with a leading '?' when the argument may be null.
struct Message {
    const char* name;
    const char* sig;
    uint32_t since;
    uint8_t destructor;
    const char* newIface;  // interface of the new_id argument, if any
};

struct Interface {
    const char* name;
    uint32_t version;
    const Message* requests;
    uint16_t nRequests;
    const Message* events;
    uint16_t nEvents;
};

struct Value {
    char t = 0;
    int32_t i = 0;
    uint32_t u = 0;        // uint, object id or new_id
    double f = 0;          // fixed
    std::string s;         // string, or the interface name of an untyped new_id
    uint32_t version = 0;  // untyped new_id
    std::vector<uint8_t> a;
    int fd = -1;
    bool isNull = false;

    static Value I(int32_t v) { Value x; x.t = 'i'; x.i = v; return x; }
    static Value U(uint32_t v) { Value x; x.t = 'u'; x.u = v; return x; }
    static Value F(double v) { Value x; x.t = 'f'; x.f = v; return x; }
    static Value S(std::string v) { Value x; x.t = 's'; x.s = std::move(v); return x; }
    static Value O(uint32_t id) { Value x; x.t = 'o'; x.u = id; x.isNull = id == 0; return x; }
    static Value N(uint32_t id) { Value x; x.t = 'n'; x.u = id; return x; }
    static Value A(std::vector<uint8_t> v) { Value x; x.t = 'a'; x.a = std::move(v); return x; }
    static Value H(int fd) { Value x; x.t = 'h'; x.fd = fd; return x; }
};
using Args = std::vector<Value>;

// Decodes a request payload. Returns false on a malformed message.
bool parseArgs(const char* sig, const uint8_t* p, size_t n, std::deque<int>& fds, Args& out);
// Appends the encoded payload (without header) and collects the fds to send alongside it.
void encodeArgs(const char* sig, const Args& in, std::vector<uint8_t>& out, std::vector<int>& fdsOut);

}  // namespace wl
}  // namespace pxr
