defmodule OtterTest do
  use ExUnit.Case
  doctest Otter

  test "greets the world" do
    assert Otter.hello() == :world
  end
end
