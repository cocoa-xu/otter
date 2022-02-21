# Otter

| OS               | arch    | Build Status |
|------------------|---------|--------------|
| Ubuntu 20.04     | x86_64  | [![CI](https://github.com/cocoa-xu/otter/actions/workflows/linux-x86_64.yml/badge.svg)](https://github.com/cocoa-xu/otter/actions/workflows/linux-x86_64.yml) |
| macOS 11 Big Sur | x86_64  | [![CI](https://github.com/cocoa-xu/otter/actions/workflows/macos-x86_64.yml/badge.svg)](https://github.com/cocoa-xu/otter/actions/workflows/macos-x86_64.yml) |

## Dependencies
- pkg-config (for finding libffi)
- libffi-dev

For macOS, libffi can be installed by HomeBrew
```shell
brew install libffi
```

For debian/ubuntu, libffi can be installed using the following command
```shell
sudo apt update
sudo apt install libffi-dev
```

## Type Correspondences
Generally, we can extern a function using the following syntax

```elixir
extern func_name(:return_type)
extern func_name(:return_type, arg_type, ...)
extern func_name(:return_type, arg_name :: arg_type, ...)
```

Note that `arg_name` is optional, which means rule 2 and 3 can be rewritten as

```elixir
extern func_name(:return_type, [arg_name :: ]arg_type, ...)
```

and if we want to further simplify (or complify?) it, we have

```elixir
extern func_name(:return_type[, [arg_name :: ]arg_type, ...])
```

### Function return type
| Syntax         | Example In C | Example in Otter | Description                                                                                         |
|----------------|--------------|------------------|-----------------------------------------------------------------------------------------------------|
| :return_type   | uint32_t     | :u32             | unsigned 32-bit integer. Return type should be the atom version of the basic types available below. |

### Basic types

| Syntax         | Example In C | Example in Otter | Description              |
|----------------|--------------|------------------|--------------------------|
| s8             | int8_t       | s8               | signed 8-bit integer.    |
| s16            | int16_t      | s16              | signed 16-bit integer.   |
| s32            | int32_t      | s32              | signed 32-bit integer.   |
| s64            | int64_t      | s64              | signed 64-bit integer.   |
| u8             | uint8_t      | u8               | unsigned 8-bit integer.  |
| u16            | uint16_t     | u16              | unsigned 16-bit integer. |
| u32            | uint32_t     | u32              | unsigned 32-bit integer. |
| u64            | uint64_t     | u64              | unsigned 64-bit integer. |
| f32            | float        | f32              | 32-bit single-precision floating-point numbers. |
| f64            | double       | f64              | 64-bit double-precision floating-point numbers. |
| c_ptr          | void *       | c_ptr            | Any C pointer.           |

```elixir
defmodule Foo do
  import Otter, except: [{:&, 1}]

  # see their implementations in test/test.cpp
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
end
```

We'll use `{T}` to indicate any **basic types** from now on. For example, `{T}-size(42)` could be `u32-size(42)`.

### ND-array types
| Syntax                | Example In C      | Example in Otter    | Description                                      |
|-----------------------|-------------------|---------------------|--------------------------------------------------|
| {T}-size(d)           | `T [d]`           | u32-size(42)        | An array of 42 unsigned 32-bit integers.       |
| {T}-size(d1, d2, ...) | `T [d1][d2][...]` | u8-size(100, 200)   | An array of 100-by-200 unsigned 8-bit integers. |

ND-array is not supported as a function argument yet. It can be only used in structs (see below) for now.

We'll use `{NDA}` to indicate any ND-array types from now on.

### Struct types
To declare C structs, we'll have to use the `cstruct` macro. 

We'll use `{FT}` to indicate the type of a field in the struct.

```shell
{FT} = {T}
       | {NDA}
```

Say you have a struct named `name`,

| Syntax                                       | Example In C                          | Example in Otter                  | Description                                                        |
|----------------------------------------------|---------------------------------------|-----------------------------------|--------------------------------------------------------------------|
| cstruct(name(field_name :: {FT}))          | `struct name { FT field_name; }`        | cstruct(name(val :: u32))         | A struct with a single field named `val` which type is `u32`       |
| cstruct(name(field_name_1 :: {FT1}, ...))  | `struct name { FT1 field_name_1; ... }` | cstruct(name(x :: f32, y :: f32)) | A struct with fields `x` and `y` and they have the same type `f32` |

## Todo
- [ ] Create struct instances using [c_struct](https://github.com/cocoa-xu/c_struct). Maybe merge code in `c_struct` to
  here?

## Demo
```elixir
defmodule Ctypes do
  import Otter, except: [{:&, 1}]

  # module level default shared library name/path
  @default_from (case :os.type() do
                   {:unix, :darwin} -> "libSystem.B.dylib"
                   {:unix, _} -> "libc.so"
                   {:win32, _} -> raise "Windows is not supported yet"
                 end)
  # module level default dlopen mode
  @default_mode :RTLD_NOW

  # specify shared library name and/or load mode for a single function
  @load_from (case :os.type() do
                {:unix, :darwin} -> "libSystem.B.dylib"
                {:unix, _} -> "libc.so"
                {:win32, _} -> raise "Windows is not supported yet"
              end)
  @load_mode :RTLD_NOW
  extern sin(:f64, f64)

  # or using module level default shared library name and load mode
  extern puts(:s32, c_ptr)
  extern dlopen(:c_ptr, c_ptr, s32)
  extern dlsym(:c_ptr, c_ptr, c_ptr)

  # explict mark argument name and type
  extern cos(:f64, theta :: f64)
  
  # also support functions with variadic arguments
  extern printf(:u64, fmt :: c_ptr, args :: va_args)
end

# one extern will define two function, 
# - one returns ok-error tuple, 
# - the other is the bang version, which returns unwrapped value on :ok, and raise RuntimeError on :error  

iex> CtypesDemo.puts("hello \r")
hello
{:ok, 10}
iex> CtypesDemo.puts!("hello \r")
hello
10
iex> CtypesDemo.printf!("world!\r\n")
world!
9
iex> CtypesDemo.sin(3.1415926535)
{:ok, 8.979318433952318e-11}
iex> CtypesDemo.sin!(3.1415926535)
8.979318433952318e-11
iex> CtypesDemo.cos!(0)
1.0
iex> CtypesDemo.cos!(0.0)
1.0
iex> CtypesDemo.printf!("%s-%.5lf-0x%08x-%c\r\n\0", [
...>   as_type!("hello world!\0", :c_ptr),
...>   as_type!(123.456789, :f64),
...>   as_type!(0xdeadbeef, :u32),
...>   as_type!(65, :u8)
...> ])
hello world!-123.45679-0xdeadbeef-A
37
iex> handle = CtypesDemo.dlopen!("/usr/lib/libSystem.B.dylib", 2) # or "libc.so" for Linux
20152781936
iex> dlsym_addr = CtypesDemo.dlsym!(handle, "dlsym")
7023526352
```

Note that we have `CtypesDemo.dlopen` and `CtypesDemo.dlsym` in the demo code above. They are declared in the `examples/ctypes_demo.ex`
file. 

```elixir
  extern dlopen(:c_ptr, c_ptr, s32)
  extern dlsym(:c_ptr, c_ptr, c_ptr)
```

And they are different from the ones in module Otter, namely, `Otter.dlopen` and `Otter.dlsym`. `CtypesDemo.dl*` are obtained
by `Otter.dlopen` and `Otter.dlsym`. 

Just like the `sin` and `cos` functions in `CtypesDemo`, `dlopen` and `dlsym` are also C functions that can be `dlsym`'ed.

`Otter.dl*` calls go to NIFs `otter_dl*` functions while `CtypesDemo.dl*` calls going to `Otter.invoke` which redirects to 
the `otter_invoke` NIF.

## Support for C struct
### basic example
```elixir
defmodule Foo do
  import Otter
  
  @default_from Path.join([__DIR__, "test.so"])
  @default_mode :RTLD_NOW
  
  # #pragma pack(push, 4)
  # struct alignas(4) s_u8_u16 {
  #     uint8_t u8;
  #     uint16_t u16;
  # };
  # #pragma pack(pop)
  cstruct(s_u8_u16(u8 :: u8, u16 :: u16))

  # please see test/test.cpp for these extern functions
  extern create_s_u8_u16(s_u8_u16())
  extern receive_s_u8_u16(:u32, t :: s_u8_u16())
end
```

### nd-array in C struct
```elixir
defmodule Foo do
  import Otter

  @default_from Path.join([__DIR__, "test.so"])
  @default_mode :RTLD_NOW
  
  # struct matrix16x16 {
  #     uint32_t m[16][16];
  # };
  cstruct(matrix16x16(m :: u32-size(16, 16)))

  # please see test/test.cpp for these extern functions
  extern create_matrix16x16(matrix16x16())
  extern receive_matrix16x16(:u32, t :: matrix16x16())
end
```

## Installation

If [available in Hex](https://hex.pm/docs/publish), the package can be installed
by adding `otter` to your list of dependencies in `mix.exs`:

```elixir
def deps do
  [
    {:otter, "~> 0.1.0"}
  ]
end
```

Documentation can be generated with [ExDoc](https://github.com/elixir-lang/ex_doc)
and published on [HexDocs](https://hexdocs.pm). Once published, the docs can
be found at <https://hexdocs.pm/otter>.

