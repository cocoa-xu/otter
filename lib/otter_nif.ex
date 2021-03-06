defmodule Otter.Nif do
  @moduledoc false

  @on_load :load_nif
  def load_nif do
    nif_file = '#{:code.priv_dir(:otter)}/otter_nif'

    case :erlang.load_nif(nif_file, 0) do
      :ok -> :ok
      {:error, {:reload, _}} -> :ok
      {:error, reason} -> IO.puts("Failed to load nif: #{reason}")
    end
  end

  def dlopen(_path, _mode), do: :erlang.nif_error(:not_loaded)
  def dlclose(_handle), do: :erlang.nif_error(:not_loaded)
  def dlsym(_image, _func_name), do: :erlang.nif_error(:not_loaded)

  def symbol_to_address(_symbol), do: :erlang.nif_error(:not_loaded)
  def address_to_symbol(_address), do: :erlang.nif_error(:not_loaded)
  def erl_nif_env(), do: :erlang.nif_error(:not_loaded)
  def stdin(), do: :erlang.nif_error(:not_loaded)
  def stdout(), do: :erlang.nif_error(:not_loaded)
  def stderr(), do: :erlang.nif_error(:not_loaded)
  def invoke(_symbol, _return_type, _args_with_type), do: :erlang.nif_error(:not_loaded)
end
