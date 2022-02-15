defmodule OtterTest do
  use ExUnit.Case
  doctest Otter
  import Otter

  @default_from Path.join([__DIR__, "test.so"])
  @default_mode :RTLD_NOW

  extern add_two_32(:u32, a :: u32, b :: u32)

  # #pragma pack(push, 4)
  # struct alignas(4) s_u8_u16 {
  #     uint8_t u8;
  #     uint16_t u16;
  # };
  # #pragma pack(pop)
  cstruct(s_u8_u16(u8 :: u8, u16 :: u16))
  extern create_s_u8_u16(s_u8_u16())
  extern receive_s_u8_u16(:u32, t :: s_u8_u16())

  # #pragma pack(push, 4)
  # struct alignas(4) s_vptr {
  #     uint8_t u8;
  #     uint16_t u16;
  #     uint32_t u32;
  #     uint64_t u64;
  #     virtual void ptr_a() {};
  # };
  # #pragma pack(pop)
  cstruct(s_vptr(vptr :: c_ptr, u8 :: u8, u16 :: u16, u32 :: u32, u64 :: u64))
  extern create_s_vptr(s_vptr())
  extern receive_s_vptr(:u32, t :: s_vptr())

  # #pragma pack(push, 4)
  # struct alignas(8) complex : public s_vptr {
  #     uint8_t c1;
  #     uint8_t c2;
  #     uint8_t c3[3];
  #     union {
  #         uint8_t u8;
  #         uint16_t u16;
  #     } foo;
  #     struct s_u8_u16 bar;
  #     virtual void ptr1() {};
  #     virtual void ptr2() {};
  #     virtual void ptr3() {};
  # };
  # #pragma pack(pop)
  cstruct(complex(vptr :: c_ptr,
    u8 :: u8, u16 :: u16, u32 :: u32, u64 :: u64, padding_1 :: u16, padding_2 :: u8, # todo: how to inherit from s_vptr
    c1 :: u8, c2 :: u8,
    c3 :: u8-size(3), # declare uint8_t c3[3]
    foo :: u16, # todo: needs some improvement, as the size of a union may not be fit in the types we have for now
    bar :: u32 # todo: embed another struct inside, bar :: s_u8_u16()
    ))
  extern create_complex(complex())
  extern receive_complex(:u32, t :: complex())

  # struct matrix16x16 {
  #     uint32_t m[16][16];
  # };
  cstruct(matrix16x16(m :: u32-size(16, 16)))
  extern create_matrix16x16(matrix16x16())
  extern receive_matrix16x16(:u32, t :: matrix16x16())

  test "add_two_32" do
    7 = add_two_32(3, 4)
  end

  test "s_u8_u16" do
    t = create_s_u8_u16()
    assert 1 == receive_s_u8_u16(t)
  end

  test "s_vptr" do
    t = create_s_vptr()
    assert 1 == receive_s_vptr(t)
  end

  test "complex" do
    t = create_complex()
    assert 1 == receive_complex(t)
  end

  test "nd-array" do
    t = create_matrix16x16()
    assert 32640 == receive_matrix16x16(t)
  end
end
