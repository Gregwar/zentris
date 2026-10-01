"""Zentris: a Tetris Effect inspired game that plays any playlist."""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("zentris")  # single source of truth: pyproject.toml
except PackageNotFoundError:
    __version__ = "unknown"
