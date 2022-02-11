defmodule Otter do
  @moduledoc """
  Documentation for `Otter`.
  """

  def otter_table_name, do: :otter

  @mode_to_int %{
    :RTLD_SELF => 0,
    :RTLD_LAZY => 0x00001,
    :RTLD_NOW => 0x00002,
    :RTLD_BINDING_MASK => 0x00003,
    :RTLD_NOLOAD => 0x00004,
    :RTLD_DEEPBIND => 0x00005,
    :RTLD_GLOBAL => 0x00100,
    :RTLD_LOCAL => 0x00000,
    :RTLD_NODELETE => 0x01000
  }

  def dlopen(nil, mode) when is_atom(mode) do
    dlopen("RTLD_SELF", Map.get(@mode_to_int, mode))
  end

  def dlopen(:RTLD_SELF, mode) when is_atom(mode) do
    dlopen("RTLD_SELF", Map.get(@mode_to_int, mode))
  end

  def dlopen(path, mode) when is_binary(path) and is_atom(mode) do
    dlopen(path, Map.get(@mode_to_int, mode))
  end

  def dlopen(path, mode) when is_binary(path) and is_integer(mode) do
    Otter.Nif.dlopen(path, mode)
  end

  def dlclose(handle) when is_reference(handle) do
    Otter.Nif.dlclose(handle)
  end

  def dlsym(image, func_name) when is_reference(image) and is_binary(func_name) do
    Otter.Nif.dlsym(image, func_name)
  end

  def invoke(symbol, return_type, args_with_type) do
    Otter.Nif.invoke(symbol, return_type, args_with_type)
  end

  defmacro decc(fun) do
    {name, args} = Macro.decompose_call(fun)
    [_ | func_args] = args
    {_name, _, [return_type | arg_types]} = fun
    arg_types =
      arg_types
      |> Enum.map(&elem(&1, 0))
      |> Enum.map(&"#{&1}")

    quote do
      def unquote(:"#{name}")(unquote_splicing(func_args)) do
        # todo: get load_from and load_mode by Module.get_attribute
        # @load_from "/usr/lib/libSystem.B.dylib"
        # @load_mode :RTLD_NOW
        load_from = "/usr/lib/libSystem.B.dylib"
        load_mode = :RTLD_NOW

        func_name = __ENV__.function |> elem(0) |> Atom.to_string()
        return_type = unquote(return_type)

        if Enum.member?(:ets.all(), Otter.otter_table_name) == false do
          :ets.new(Otter.otter_table_name, [:public, :named_table])
        end

        image =
          with [] <- :ets.lookup(Otter.otter_table_name, load_from) do
            {:ok, image} = dlopen(load_from, load_mode)
            :ets.insert(Otter.otter_table_name, [{load_from, image}])
            image
          else
            [{^load_from, image}|_] -> image
            {:error, reason} -> raise RuntimeError, reason
          end
        true = is_reference(image)

        image_function = "#{load_from}.#{func_name}"
        symbol =
          with [] <- :ets.lookup(Otter.otter_table_name, image_function),
               {:dlsym, {:ok, symbol}} <- {:dlsym, dlsym(image, func_name)} do
            :ets.insert(Otter.otter_table_name, [{image_function, symbol}])
            symbol
          else
            [{^image_function, symbol}|_] -> symbol
            {:dlsym, {:error, reason}} -> IO.puts("1: #{IO.inspect(reason)}")
            {:error, reason} -> IO.puts("2: #{IO.inspect(reason)}")
          end
        Otter.invoke(symbol, return_type, Enum.zip([unquote_splicing(func_args)], unquote(arg_types)))
      end
    end
  end
end
