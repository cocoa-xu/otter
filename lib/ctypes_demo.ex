defmodule Ctypes do
  import Otter, except: [{:dlopen, 2}, {:dlsym, 2}]

  # module level default shared library name/path
  @default_from (case :os.type() do
                   {:unix, :darwin} -> "libSystem.B.dylib"
                   {:unix, _} -> "libc.so"
                   {:win32, _} -> raise "Windows is not supported yet"
                 end)
  # module level default dlopen mode
  @default_mode :RTLD_NOW

  # specify shared library name and load mode for a single function
  @load_from (case :os.type() do
                {:unix, :darwin} -> "libSystem.B.dylib"
                {:unix, _} -> "libc.so"
                {:win32, _} -> raise "Windows is not supported yet"
              end)
  @load_mode :RTLD_NOW
  extern(sin(:f64, f64))

  # or using module level default shared library name and load mode
  extern(puts(:s32, c_ptr))
  extern(printf(:s32, c_ptr))
  extern(printf(:s32, c_ptr, va_args))
  extern(dlopen(:c_ptr, c_ptr, s32))
  extern(dlsym(:c_ptr, c_ptr, c_ptr))

  # explict mark argument name and type
  extern(cos(:f64, theta :: f64))
  extern(tan(:f64, theta :: f64))
end
