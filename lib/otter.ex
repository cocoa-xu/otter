defmodule Otter do
  @moduledoc """
  Documentation for `Otter`.
  """

  @mode_to_int %{
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

  def symbol_addr(symbol) do
    Otter.Nif.symbol_addr(symbol)
  end

  def invoke(symbol, return_type, args_with_type) do
    Otter.Nif.invoke(symbol, return_type, args_with_type)
  end

  defmacro extern(fun) do
    {name, args} = Macro.decompose_call(fun)
    [return_type | func_args] = args

    func_arg_types =
      func_args
      |> Enum.with_index(fn element, index -> {element, index} end)
      |> Enum.map(fn {{arg_name, line, extra}, index} ->
        case extra do
          nil ->
            unique_arg_name =
              arg_name
              |> Atom.to_string()
              |> then(&"#{&1}_#{index}")
              |> String.to_atom()

            {{unique_arg_name, line, extra}, "#{Atom.to_string(arg_name)}"}

          [{arg_name, line, _}, {arg_type, _, _}] ->
            arg_name =
              arg_name
              |> Atom.to_string()
              |> then(&"#{&1}_#{index}")
              |> String.to_atom()

            {{arg_name, line, nil}, "#{Atom.to_string(arg_type)}"}
        end
      end)

    func_args =
      func_arg_types
      |> Enum.map(&elem(&1, 0))

    arg_types =
      func_arg_types
      |> Enum.map(&elem(&1, 1))

    quote do
      @load_from Module.get_attribute(
                   __MODULE__,
                   :load_from,
                   Module.get_attribute(__MODULE__, :default_from)
                 )
      @load_mode Module.get_attribute(
                   __MODULE__,
                   :load_mode,
                   Module.get_attribute(__MODULE__, :default_mode)
                 )
      def unquote(:"#{name}")(unquote_splicing(func_args)) do
        func_name = __ENV__.function |> elem(0) |> Atom.to_string()
        return_type = unquote(return_type)

        with {:ok, image} <- Otter.dlopen(@load_from, @load_mode),
             {:ok, symbol} <- Otter.dlsym(image, func_name) do
          Otter.invoke(
            symbol,
            return_type,
            Enum.zip([unquote_splicing(func_args)], unquote(arg_types))
          )
        else
          {:error, reason} -> raise reason
        end
      end
    end
  end
end
