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
  # struct alignas(8) complex {
  #     uint8_t c1;
  #     uint8_t c2;
  #     uint8_t c3[3];
  #     union {
  #         uint8_t u8;
  #         uint16_t u16;
  #     } foo;
  #     struct s_u8_u16 bar;
  # };
  # #pragma pack(pop)
  cstruct(complex(
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

  # test basic data types
  extern pass_through_u8(:u8, val :: u8)
  extern pass_through_u16(:u16, val :: u16)
  extern pass_through_u32(:u32, val :: u32)
  extern pass_through_u64(:u64, val :: u64)
  extern pass_through_s8(:s8, val :: s8)
  extern pass_through_s16(:s16, val :: s16)
  extern pass_through_s32(:s32, val :: s32)
  extern pass_through_s64(:s64, val :: s64)
  extern pass_through_f32(:f32, val :: f32)
  extern pass_through_f64(:f64, val :: f64)
  extern pass_through_c_ptr(:u64, ptr :: c_ptr)

  extern multiply(:u64, a :: u32, b :: u32)
  extern divide(:u64, a :: u32, b :: u32)
  extern add(:u64, a :: u32, b :: u32)
  extern subtract(:u64, a :: u32, b :: u32)
  extern pass_func_ptr(:u64, a :: u32, b :: u32, op :: c_ptr)

  test "add_two_32" do
    IO.inspect("add_two_32 start")
    7 = add_two_32!(3, 4)
    IO.inspect("add_two_32 end")

    IO.inspect("bdt start")
    42 = pass_through_u8!(42)
    IO.inspect("bdt 1")
    65535 = pass_through_u16!(65535)
    IO.inspect("bdt 2")
    0xdeadbeef = pass_through_u32!(0xdeadbeef)
    IO.inspect("bdt 3")
    0xfeedfacedeadbeef = pass_through_u64!(0xfeedfacedeadbeef)
    IO.inspect("bdt 4")
    -42 = pass_through_s8!(-42)
    IO.inspect("bdt 5")
    -32000 = pass_through_s16!(-32000)
    IO.inspect("bdt 6")
    -559038737 = pass_through_s32!(0xdeadbeef)
    IO.inspect("bdt 7")
    -123456789 = pass_through_s64!(-123456789)
    IO.inspect("bdt 8")
    assert 0.001 > abs(123.0125 - pass_through_f32!(123.0125))
    IO.inspect("bdt 9")
    -123.456 = pass_through_f64!(-123.456)
    IO.inspect("bdt 10")
    0 = pass_through_c_ptr!(0)
    IO.inspect("bdt 11")
    0xdeadbeef = pass_through_c_ptr!(0xdeadbeef)
    IO.inspect("bdt 12")

    IO.inspect("openclose6")
    {:ok, image} = Otter.dlopen(@default_from, :RTLD_NOW)
    IO.inspect("openclose5")
    {:ok, add_two_32} = Otter.dlsym(image, "add_two_32")
    IO.inspect("openclose4")
    {:ok, add_two_32_addr} = Otter.symbol_to_address(add_two_32)
    IO.inspect("openclose3")
    {:ok, _add_two_32_sym} = Otter.address_to_symbol(add_two_32_addr)
    IO.inspect("openclose1222")
    IO.inspect("openclose122324532")

    IO.inspect("self start")
    {:ok, _image} = Otter.dlopen(nil, :RTLD_NOW)
    IO.inspect("self end")

    IO.inspect("func ptr start")
    {:ok, image} = Otter.dlopen(@default_from, :RTLD_NOW)
    multiply =
      image
      |> Otter.dlsym!("multiply")
    IO.inspect("func ptr mu")
    divide =
      image
      |> Otter.dlsym!("divide")
    IO.inspect("func ptr d")
    add =
      image
      |> Otter.dlsym!("add")
    IO.inspect("func ptr a")
    subtract =
      image
      |> Otter.dlsym!("subtract")
    IO.inspect("func ptr s")
    # pass function pointer by symbol
    1008 = pass_func_ptr!(42, 24, multiply)
    1 = pass_func_ptr!(42, 24, divide)
    66 = pass_func_ptr!(42, 24, add)
    18 = pass_func_ptr!(42, 24, subtract)
    IO.inspect("func ptr symb")

    # pass function pointer by address
    1008 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(multiply))
    1 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(divide))
    66 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(add))
    18 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(subtract))
    IO.inspect("func ptr addrend")
  end
end
