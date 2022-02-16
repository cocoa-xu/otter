#include <iostream>

using namespace std;

struct s_u8_u16 {
    uint8_t u8;
    uint16_t u16;
};

struct complex {
    uint8_t c1;
    uint8_t c2;
    uint8_t c3[3];
    union {
        uint8_t u8;
        uint16_t u16;
    } foo;
    struct s_u8_u16 bar;
};

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

struct complex create_complex() {
    struct complex t;
    t.c1 = 'c';
    t.c2 = 'd';
    t.c3[0] = 'e';
    t.c3[1] = 'f';
    t.c3[2] = 'g';
    t.foo.u16 = 32768;
    t.bar.u8 = 'i';
    t.bar.u16 = 49152;
    return t;
}

uint32_t receive_complex(struct complex t) {
    return (
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

uint64_t multiply_in_test(uint32_t a, uint32_t b) {
    return a * b;
}

uint64_t divide_in_test(uint32_t a, uint32_t b) {
    return a / b;
}

uint64_t add_in_test(uint32_t a, uint32_t b) {
    return a + b;
}

uint64_t subtract_in_test(uint32_t a, uint32_t b) {
    return a - b;
}

uint64_t pass_func_ptr(uint32_t a, uint32_t b, uint64_t(*op)(uint32_t, uint32_t)) {
    return op(a, b);
}

}
