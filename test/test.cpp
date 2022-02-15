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

struct matrix16x16 {
    uint32_t m[16][16];
};

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

struct matrix16x16 create_matrix16x16() {
    struct matrix16x16 t;
    for (size_t i = 0; i < 16; i++) {
        for (size_t j = 0; j < 16; ++j) {
            t.m[i][j] = 16 * i + j;
        }
    }
    return t;
}

uint32_t receive_matrix16x16(struct matrix16x16 t) {
    uint32_t sum = 0;
    for (size_t i = 0; i < 16; i++) {
        for (size_t j = 0; j < 16; ++j) {
            sum += t.m[i][j];
        }
    }
    return sum;
}

uint32_t add_two_32(uint32_t a, uint32_t b) {
    return a + b;
}

uint8_t pass_through_u8(uint8_t val) {
    return val;
}

uint16_t pass_through_u16(uint16_t val) {
    return val;
}

uint32_t pass_through_u32(uint32_t val) {
    return val;
}

uint64_t pass_through_u64(uint64_t val) {
    return val;
}

int8_t pass_through_s8(int8_t val) {
    return val;
}

int16_t pass_through_s16(int16_t val) {
    return val;
}

int32_t pass_through_s32(int32_t val) {
    return val;
}

int64_t pass_through_s64(int64_t val) {
    return val;
}

float pass_through_f32(float val) {
    return val;
}

double pass_through_f64(double val) {
    return val;
}

uint64_t pass_through_c_ptr(void * ptr) {
    return (uint64_t)(uint64_t *)ptr;
}
}
