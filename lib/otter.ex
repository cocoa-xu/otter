defmodule Otter do
  @moduledoc """
  Documentation for `Otter`.
  """

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

  @doc """
  open
  """
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

  @doc """
  decc stands for `declare C` (function)
  """
  defmacro decc(fun) do
    {name, args} = Macro.decompose_call(fun)
    [_ | func_args] = args
    {_name, _, [return_type | arg_types]} = fun
    arg_types =
      arg_types
      |> Enum.map(&elem(&1, 0))
      |> Enum.map(&"#{&1}")

    quote do
      @load_from Module.get_attribute(__MODULE__, :load_from, Module.get_attribute(__MODULE__, :default_from))
      @load_mode Module.get_attribute(__MODULE__, :load_mode, Module.get_attribute(__MODULE__, :default_mode))
      def unquote(:"#{name}")(unquote_splicing(func_args)) do
        func_name = __ENV__.function |> elem(0) |> Atom.to_string()
        return_type = unquote(return_type)

        with {:ok, image} <- dlopen(@load_from, @load_mode),
             {:ok, symbol} <- dlsym(image, func_name) do
          Otter.invoke(symbol, return_type, Enum.zip([unquote_splicing(func_args)], unquote(arg_types)))
        else
          {:error, reason} -> raise reason
        end
      end
    end
  end
end
