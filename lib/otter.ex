defmodule Otter do
  @moduledoc """
  Documentation for `Otter`.
  """

  import Otter.Errorize

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
  dlopen a shared library at given path

  - `path`: if `path` is `nil`, `:RTLD_SELF` or `"RTLD_SELF"`, then open self

     Otherwise, the path will pass to the C function `dlopen` as is.

  - `mode`: dlopen mode. See man (3) dlopen for details.
  """
  def dlopen(nil, mode) when is_atom(mode) do
    dlopen(:RTLD_SELF, mode)
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

  deferror dlopen(path, mode)

  @doc """
  Call dlclose to release a opened handle

  Note that the underlying implementation of dlclose and the OS can decide whether to
  actually unload the shared library.

  - `handle`: The handle of an opened shared library
  """
  def dlclose(handle) when is_reference(handle) do
    Otter.Nif.dlclose(handle)
  end

  deferror dlclose(handle)

  @doc """
  Find a symbol in an image

  - `image`: A valid image(shared library) handle
  - `func_name`: The function name to look for in the image.
  """
  def dlsym(image, func_name) when is_reference(image) and is_binary(func_name) do
    Otter.Nif.dlsym(image, func_name)
  end

  deferror dlsym(image, func_name)

  @doc """
  Get the raw address of a symbol.
  """
  def symbol_to_address(symbol) do
    Otter.Nif.symbol_to_address(symbol)
  end

  deferror symbol_to_address(symbol)

  @doc """
  Convert the raw address to a symbol. Use with cautions.
  """
  def address_to_symbol(address) do
    Otter.Nif.address_to_symbol(address)
  end

  deferror address_to_symbol(address)

  @doc """
  Get current erlang NIF environment
  """
  def erl_nif_env() do
    Otter.Nif.erl_nif_env()
  end

  deferror erl_nif_env()

  @doc """
  Invoke a symbol(function) with input arguments

  - `symbol`: Function to call
  - `return_type`: an atom that specifies the function's return type
  - `args_with_type`: a list of 2-tuples. An example of a valid `args_with_type` when
     calling cos(3.1415926)

    ```elixir
    [
      {3.1415926, %{type: "f64"}}
    ]
    ```

    Note that the first element in the tuple should be the value of the input argument, and
    the second element should be a map.

    The map MUST contain a `type` key, and the corresponding value (either a string or an atom)
    specifies the type of the first element in the tuple.
  """
  def invoke(symbol, return_type, args_with_type) do
    Otter.Nif.invoke(symbol, return_type, args_with_type)
  end

  deferror invoke(symbol, return_type, args_with_type)

  defp get_unique_arg_name(arg_name, index) do
    arg_name
    |> Atom.to_string()
    |> then(&"#{&1}_#{index}")
    |> String.to_atom()
  end

  defmodule CStruct do
    defstruct fields: [], id: nil
  end

  def transform_type(%CStruct{fields: fields, id: id}) do
    {:struct, id, fields}
  end

  def transform_type(name) when is_atom(name) do
    name
  end

  defmacro extern(fun) do
    {name, args} = Macro.decompose_call(fun)
    [return_type | func_args] = args

    func_arg_types =
      func_args
      |> Enum.with_index(fn element, index -> {element, index} end)
      |> Enum.map(fn
        {{struct_name, line, []}, index} when is_atom(struct_name) ->
          unique_arg_name = get_unique_arg_name(struct_name, index)

          struct_tuple =
            quote do
              unquote(struct_name)() |> Otter.transform_type()
            end

          {{unique_arg_name, line, nil}, struct_tuple}

        {{arg_name, line, extra}, index} ->
          case extra do
            nil ->
              unique_arg_name = get_unique_arg_name(arg_name, index)
              {{unique_arg_name, line, extra}, "#{Atom.to_string(arg_name)}"}

            [{arg_name, _line, _}, {arg_type, _, _}] ->
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
        return_type = unquote(return_type) |> Otter.transform_type()

        with {:ok, image} <- Otter.dlopen(@load_from, @load_mode),
             {:ok, symbol} <- Otter.dlsym(image, func_name) do
          type_info =
            Enum.map([unquote_splicing(arg_types)], fn cur_type ->
              %{type: cur_type}
            end)
          Otter.invoke(
            symbol,
            return_type,
            Enum.zip([unquote_splicing(func_args)], type_info)
          )
        else
          {:error, reason} -> raise reason
        end
      end
      deferror(unquote(:"#{name}")(unquote_splicing(func_args)))
    end
  end

  defmacro cstruct(declaration) do
    {name, fields} = Macro.decompose_call(declaration)
    name = Atom.to_string(name)

    [fields, extra_info] =
      fields
      |> Enum.reduce([[], []], fn type_identifier, [fields, extra_info] ->
        case type_identifier do
          {:"::", _, [{arg_name, _, nil}, {type_name, _, nil}]} ->
            [
              [{arg_name, type_name} | fields],
              [[] | extra_info]
            ]
          {:"::", _, [{arg_name, _, nil}, {:-, _, [{type_name, _, nil}, {:size, _, array_size}]}]} ->
            array_size = array_size |> List.to_tuple() |> Tuple.product()
            [
              [{arg_name, type_name} | fields],
              [[{:size, array_size}] | extra_info]
            ]
        end
      end)
    fields = Enum.reverse(fields)
    extra_info = Enum.reverse(extra_info)

    quote do
      def unquote(:"#{name}")() do
        fields = unquote(fields)
        extra_info = unquote(extra_info)
        fields_with_extra_info =
          Enum.zip(fields, extra_info) |> Enum.map(fn {{field_name, field_type}, extra} ->
            case extra do
              [] ->
                {field_name, %{type: field_type}}
              _ ->
                map =
                  Enum.reduce(extra, %{type: field_type}, fn {extra_name, extra_value}, map ->
                    Map.put(map, extra_name, extra_value)
                  end)
                {field_name, map}
            end
          end)
        struct(Otter.CStruct, fields: fields_with_extra_info, id: unquote(:"#{name}"))
      end
    end
  end
end
