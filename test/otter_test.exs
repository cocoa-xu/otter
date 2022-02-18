defmodule OtterTest do
  use ExUnit.Case
  doctest Otter
  import Otter

  @default_from Path.join([__DIR__, "test.so"])
  @default_mode :RTLD_NOW

  extern add_two_32(:u32, a :: u32, b :: u32)

  # struct s_u8_u16 {
  #     uint8_t u8;
  #     uint16_t u16;
  # };
  cstruct(s_u8_u16(u8 :: u8, u16 :: u16))
  extern create_s_u8_u16(s_u8_u16())
  extern receive_s_u8_u16(:u32, s_u8_u16)

  # struct s_uints {
  #     uint8_t u8;
  #     uint16_t u16;
  #     uint32_t u32;
  #     uint64_t u64;
  # };
  cstruct(s_uints(u8 :: u8, u16 :: u16, u32 :: u32, u64 :: u64))
  extern create_s_uints(s_uints())
  extern receive_s_uints(:u32, t :: s_uints())

  # struct complex {
  #     uint8_t c1;
  #     uint8_t c2;
  #     uint8_t c3[3];
  #     union {
  #         uint8_t u8;
  #         uint16_t u16;
  #     } foo;
  #     struct s_u8_u16 bar;
  # };
  cstruct(complex(
    c1 :: u8, c2 :: u8,
    c3 :: u8-size(3), # declare uint8_t c3[3]
    foo :: u16, # todo: needs some improvement, as the size of a union may not be fit in the types we have for now
    bar :: u32 # todo: embed another struct inside, bar :: s_u8_u16()
    ))
  extern create_complex(complex())
  extern receive_complex(:u32, c :: complex())

  # struct matrix16x16 {
  #     uint32_t m[16][16];
  # };
  cstruct(matrix16x16(m :: u32-size(16, 16)))
  extern create_matrix16x16(matrix16x16())
  extern receive_matrix16x16(:u32, matrix16x16())

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

  extern multiply_in_test(:u64, a :: u32, b :: u32)
  extern divide_in_test(:u64, a :: u32, b :: u32)
  extern add_in_test(:u64, a :: u32, b :: u32)
  extern subtract_in_test(:u64, a :: u32, b :: u32)
  extern pass_func_ptr(:u64, a :: u32, b :: u32, op :: c_ptr)
  
  extern func_return_type_void(:void)

  test "add_two_32" do
    7 = add_two_32!(3, 4)
  end

  test "s_u8_u16" do
    t = create_s_u8_u16!()
    assert 1 == receive_s_u8_u16!(t)
  end

  test "s_uints" do
    t = create_s_uints!()
    assert 1 == receive_s_uints!(t)
  end

  test "complex-parallel" do
    for i <- 0..:erlang.system_info(:logical_processors) do
      Task.async(fn ->
        t = create_complex!()
        assert 1 == receive_complex!(t)
        i
      end)
    end
    |> Task.await_many
  end

  test "complex" do
    t = create_complex!()
    assert 1 == receive_complex!(t)
  end

  test "nd-array" do
    t = create_matrix16x16!()
    assert 32640 == receive_matrix16x16!(t)
  end

  test "basic data types" do
    42 = pass_through_u8!(42)
    65535 = pass_through_u16!(65535)
    0xdeadbeef = pass_through_u32!(0xdeadbeef)
    0xfeedfacedeadbeef = pass_through_u64!(0xfeedfacedeadbeef)
    -42 = pass_through_s8!(-42)
    -32000 = pass_through_s16!(-32000)
    -559038737 = pass_through_s32!(0xdeadbeef)
    -123456789 = pass_through_s64!(-123456789)
    assert 0.001 > abs(123.0125 - pass_through_f32!(123.0125))
    -123.456 = pass_through_f64!(-123.456)
    0 = pass_through_c_ptr!(0)
    0xdeadbeef = pass_through_c_ptr!(0xdeadbeef)
  end

  test "dlopen, dlclose, symbol_to_address and address_to_symbol" do
    {:ok, image} = Otter.dlopen(@default_from, :RTLD_NOW)
    {:ok, add_two_32} = Otter.dlsym(image, "add_two_32")
    {:ok, add_two_32_addr} = Otter.symbol_to_address(add_two_32)
    {:ok, _add_two_32_sym} = Otter.address_to_symbol(add_two_32_addr)
  end

  test "dlopen self" do
    {:ok, _image} = Otter.dlopen(nil, :RTLD_NOW)
  end

  test "pass function pointer by symbol/address" do
    {:ok, image} = Otter.dlopen(@default_from, :RTLD_NOW)
    multiply =
      image
      |> Otter.dlsym!("multiply_in_test")

    divide =
      image
      |> Otter.dlsym!("divide_in_test")

    add =
      image
      |> Otter.dlsym!("add_in_test")

    subtract =
      image
      |> Otter.dlsym!("subtract_in_test")

    # pass function pointer by symbol
    1008 = pass_func_ptr!(42, 24, multiply)
    1 = pass_func_ptr!(42, 24, divide)
    66 = pass_func_ptr!(42, 24, add)
    18 = pass_func_ptr!(42, 24, subtract)

    # pass function pointer by address
    1008 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(multiply))
    1 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(divide))
    66 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(add))
    18 = pass_func_ptr!(42, 24, Otter.symbol_to_address!(subtract))
  end
  
  test "void_return_type" do
    func_return_type_void!()
  end
end
