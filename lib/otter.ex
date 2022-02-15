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

  def erl_nif_env() do
    Otter.Nif.erl_nif_env()
  end

  def invoke(symbol, return_type, args_with_type) do
    Otter.Nif.invoke(symbol, return_type, args_with_type)
  end

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
