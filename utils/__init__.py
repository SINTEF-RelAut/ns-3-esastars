"""Compatibility helpers for ns-3 build scripts.

This package exists alongside the top-level utils.py module. The build system
imports ``read_config_file`` from ``utils``, so we re-export the implementation
from the root module here to avoid an import collision with this package.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path


def _load_root_utils_module():
	root_utils_path = Path(__file__).resolve().parent.parent / "utils.py"
	spec = importlib.util.spec_from_file_location("ns3_root_utils", root_utils_path)
	if spec is None or spec.loader is None:
		raise ImportError(f"Unable to load root utils module from {root_utils_path}")

	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


_root_utils = _load_root_utils_module()
read_config_file = _root_utils.read_config_file

__all__ = ["read_config_file"]
