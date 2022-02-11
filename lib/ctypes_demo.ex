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

  # specify shared library name and load mode for a single function
  @load_from "libSystem.B.dylib"
  @load_mode :RTLD_NOW
  decc sin(:f64, f64)

  # or using module level default shared library name and load mode
  decc puts(:s32, c_ptr)
  decc printf(:s32, c_ptr)
  decc printf(:s32, c_ptr, va_args)
end
