#include <iostream>

using namespace std;

#pragma pack(push, 4)
struct alignas(4) s_u8_u16 {
    uint8_t u8;
    uint16_t u16;
};
#pragma pack(pop)

#pragma pack(push, 4)
struct alignas(4) s_vptr {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    virtual void ptr_a() {};
};
#pragma pack(pop)

#pragma pack(push, 4)
struct alignas(8) complex : public s_vptr {
    uint8_t c1;
    uint8_t c2;
    uint8_t c3[3];
    union {
        uint8_t u8;
        uint16_t u16;
    } foo;
    struct s_u8_u16 bar;
    virtual void ptr1() {};
    virtual void ptr2() {};
    virtual void ptr3() {};
};
#pragma pack(pop)

extern "C" {
struct s_u8_u16 create_s_u8_u16() {
    struct s_u8_u16 t;
    t.u8 = 'a';
    t.u16 = 42 << 10; // 43008
    return t;
}

uint32_t receive_s_u8_u16(struct s_u8_u16 t) {
    return (t.u8 == 'a' && t.u16 == (42 << 10));
}

struct s_vptr create_s_vptr() {
    struct s_vptr t;
    t.u8 = 'b';
    t.u16 = 65535;
    t.u32 = 0xdeadbeef;
    t.u64 = 0xfeedfacedeadbeef;
    return t;
}

uint32_t receive_s_vptr(struct s_vptr t) {
    return (
        t.u8 == 'b' && 
        t.u16 == 65535 &&
        t.u32 == 0xdeadbeef && 
        t.u64 == 0xfeedfacedeadbeef
    );
}

struct complex create_complex() {
    struct complex t;
    t.c1 = 'c';
    t.c2 = 'd';
    t.c3[0] = 'e';
    t.c3[1] = 'f';
    t.c3[2] = 'g';
    t.foo.u16 = 32768;
    t.u8 = 'h';
    t.u16 = 65535;
    t.u32 = 0xdeadbeef;
    t.u64 = 0xfeedfacedeadbeef;
    t.bar.u8 = 'i';
    t.bar.u16 = 49152;
    return t;
}

uint32_t receive_complex(struct complex t) {
    struct complex sample;
    void * received_vtable = *(void ***)&t;
    void * sample_vtable = *(void ***)&sample;
    if (received_vtable != sample_vtable) {
        printf("vtable does not match: %p != %p\r\n", received_vtable, sample_vtable);
        return 2;
    }

    return (
        t.u8 == 'h' &&
        t.u16 == 65535 &&
        t.u32 == 0xdeadbeef &&
        t.u64 == 0xfeedfacedeadbeef &&
        t.c1 == 'c' &&
        t.c2 == 'd' &&
        t.c3[0] == 'e' &&
        t.c3[1] == 'f' &&
        t.c3[2] == 'g' &&
        t.foo.u16 == 32768 &&
        t.bar.u8 == 'i' &&
        t.bar.u16 == 49152
    );
}

uint32_t add_two_32(uint32_t a, uint32_t b) {
    return a + b;
}
}
