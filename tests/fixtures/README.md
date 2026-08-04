# Custom Component Fixtures

Register every custom driver and decoder in `config/custom-components.json`.

Driver entries run a non-destructive `sigrok-cli` command and assert its output.
Decoder entries must name a fixture below this directory, pass its path through
`{fixture}` in `arguments`, and assert stable decoded output. Keep captures and
their expected output small, deterministic, and free of proprietary data.

The package verifier runs every registered fixture. A component is not ready
for release until its registry entry and fixture pass on the packaged runtime.
