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
end
