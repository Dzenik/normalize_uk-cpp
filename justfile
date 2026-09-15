# Show available development commands.
default:
    @just --list

# Install Python development dependencies and the editable package.
setup:
    uv sync --group dev

# Apply Ruff lint fixes and format Python files.
format:
    uv run ruff check --fix .
    uv run ruff format .

# Check Python formatting without changing files.
format-check:
    uv run ruff format --check .

# Run Ruff's Python lint rules.
ruff:
    uv run ruff check .

# Check Python types with Pyright.
typecheck:
    uv run pyright

# Run all Python static checks.
lint: ruff format-check typecheck

# Run Python binding tests.
test-python:
    uv run python tests/python_binding_tests.py

# Configure and build the C++ project and tests.
build-cpp:
    cmake -S . -B build -DNORMALIZE_UK_CPP_BUILD_TESTS=ON
    cmake --build build --parallel

# Build and run C++ tests.
test-cpp: build-cpp
    ctest --test-dir build --output-on-failure

# Run Python and C++ tests.
test: test-python test-cpp

# Run static checks and all tests.
check: lint test

# Build a wheel for a chosen Python version, for example: just wheel 3.15.
wheel python="3.12":
    uv build --wheel --python {{python}} --out-dir build/wheels
