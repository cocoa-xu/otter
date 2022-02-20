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

  extern pass_by_addr_read_only(:u32, val_addr :: u32-addr)
  extern pass_by_addr_read_write(:u32, val_addr :: u32-addr-out)

  extern variadic_func_pass_by_values(:u64, n :: u32, array :: va_args)

  extern fopen(:u64, path :: c_ptr, mode :: c_ptr)
  extern fclose(:u32, stream :: c_ptr)
  extern fscanf(:u64, stream :: c_ptr, fmt :: c_ptr, args :: va_args)
  extern fprintf(:u64, stream :: c_ptr, fmt :: c_ptr, args :: va_args)

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

  test "pass argument by address/read only" do
    assert 1 == pass_by_addr_read_only!(1)
  end

  test "pass argument by address/read write" do
    {return_val, out_values} = pass_by_addr_read_write!(1)
    assert 1 == return_val
    assert [2] = out_values
  end

  test "variadic function/arguments passed by values" do
    val = [1, 2, 3, 4, 5]
    n = Enum.count(val)
    sum = Enum.sum(val)
    val =
      val
      |> Enum.map(fn v ->
        Otter.as_type!(v, :u32)
      end)
    ^sum = variadic_func_pass_by_values!(n, val)
  end

  test "fprintf" do
    # remove output file if exists
    test_file_path = Path.join([__DIR__, "test_fprintf.txt"])
    File.rm_rf!(test_file_path)

    # ensure NULL-terminated string
    test_stream = fopen!(test_file_path <> "\0", "w\0")
    assert test_stream != 0
    fprintf!(test_stream, "%s-%.5lf-0x%08x-%c\r\n\0", [
      as_type!("hello world!\0", :c_ptr),
      as_type!(123.456789, :f64),
      as_type!(0xdeadbeef, :u32),
      as_type!(65, :u8)
    ])
    fclose!(test_stream)
    "hello world!-123.45679-0xdeadbeef-A\r\n" = File.read!(test_file_path)
    File.rm_rf!(test_file_path)
  end

  test "variadic args out" do
    # prepare test file
    u32_val = 42
    f64_val = 42.42424242
    test_file_path = Path.join([__DIR__, "test_fscanf.txt"])
    :ok = File.write!(test_file_path, "#{u32_val} #{f64_val}\r\n")

    # ensure NULL-terminated string
    test_stream = fopen!(test_file_path <> "\0", "r\0")
    # test for NULL
    assert test_stream != 0
    {return_val, out_vals} =
      fscanf!(test_stream, "%d %lf\0", [
        as_type!(0, :u32) |> pass_by!(:addr, :out),
        as_type!(0, :f64) |> pass_by!(:addr, :out),
      ])
    fclose!(test_stream)
    {2, [^u32_val, ^f64_val]} = {return_val, out_vals}
    File.rm_rf!(test_file_path)
  end
end
