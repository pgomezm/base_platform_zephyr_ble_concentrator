"""Shared fixtures and command-line options.

Same shape as deepsight-polaris-software/tests/pytest/conftest.py: the path
fix-up so `utils` is importable, pytest_addoption for the bench parameters, and
fixtures that hand them to the tests.
"""

import sys
from pathlib import Path

import pytest

# Add tests directory to Python path so we can import from utils
tests_dir = Path(__file__).parent.parent
sys.path.insert(0, str(tests_dir))


def pytest_addoption(parser):
    parser.addoption(
        "--host",
        action="store",
        default="0.0.0.0",
        help="address the uplink receiver binds to (default: every interface)",
    )

    parser.addoption(
        "--port",
        action="store",
        type=int,
        default=5000,
        help="TCP port the concentrator connects to (default: 5000)",
    )


@pytest.fixture(scope="session")
def uplink_config(request):
    """Where a live concentrator would connect, for tests that need hardware."""
    return {
        "host": request.config.getoption("--host"),
        "port": request.config.getoption("--port"),
    }
