# Otter
## Demo
```elixir
defmodule Ctypes do
  import Otter

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
  extern printf(:s32, c_ptr)
  extern printf(:s32, c_ptr, va_args)
  extern dlopen(:c_ptr, c_ptr, s32)
  extern dlsym(:c_ptr, c_ptr, c_ptr)

  # explict mark argument name and type
  extern cos(:f64, theta :: f64)
end

iex(1)> Ctypes.puts("hello \r")
hello
10
iex(2)> Ctypes.printf("world!\r\n")
world!
9
iex(3)> Ctypes.sin(3.1415926535)
8.979318433952318e-11
iex(4)> Ctypes.cos(0)
1.0
iex(5)> Ctypes.cos(0.0)
1.0
iex(6)> handle = Ctypes.dlopen("/usr/lib/libSystem.B.dylib", 2) # or "libc.so" for Linux
20152781936
iex(7)> dlsym_addr = Ctypes.dlsym(handle, "dlsym")
7023526352
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

