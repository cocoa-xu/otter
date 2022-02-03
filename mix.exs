defmodule Otter.MixProject do
  use Mix.Project

  @github_url "https://github.com/cocoa-xu/otter"
  def project do
    [
      app: :otter,
      version: "0.1.0",
      elixir: "~> 1.13",
      start_permanent: Mix.env() == :prod,
      compilers: [:elixir_make] ++ Mix.compilers(),
      description: description(),
      package: package(),
      deps: deps(),
      source_url: @github_url
    ]
  end

  def application do
    [
      extra_applications: [:logger]
    ]
  end

  defp description() do
    "Interact with dynamic libraries."
  end

  defp deps do
    [
      {:elixir_make, "~> 0.6"},
      {:ex_doc, "~> 0.27", only: :dev, runtime: false}
    ]
  end

  defp package() do
    [
      name: "otter",
      files: ~w(c_src lib .formatter.exs mix.exs README* LICENSE* Makefile),
      licenses: ["Apache-2.0"],
      links: %{"GitHub" => @github_url}
    ]
  end
end
