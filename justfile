format:
    uv run ruff check --fix .
    uv run ruff format .

lint:
    uv run ruff check .
    uv run ruff format --check .
    uv run pyright
