defmodule Ctypes do
  import Otter

  @load_from "/usr/lib/libSystem.B.dylib"
  @load_mode :RTLD_NOW
  decc puts(:s32, c_ptr)

  @load_from "/usr/lib/libSystem.B.dylib"
  @load_mode :RTLD_NOW
  decc printf(:s32, c_ptr)

  @load_from "/usr/lib/libSystem.B.dylib"
  @load_mode :RTLD_NOW
  decc printf(:s32, c_ptr, va_args)

  @load_from "/usr/lib/libSystem.B.dylib"
  @load_mode :RTLD_NOW
  decc sin(:f64, f64)
end
