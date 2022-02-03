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

  def dlopen(path, mode) when is_binary(path) and is_atom(mode) do
    dlopen(path, Map.get(@mode_to_int, mode))
  end

  def dlopen(path, mode) when is_binary(path) and is_integer(mode) do
    Otter.Nif.dlopen(path, mode)
  end

  def dlclose(handle) when is_reference(handle) do
    Otter.Nif.dlclose(handle)
  end
end
