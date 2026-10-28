# Example launcher addon

An addon with a launcher page, an importer and a build hook, run by a helper program. The protocol is in `docs/design/addons-and-mods.md`, section 2, Third-party addons.

| Path | What it is |
|---|---|
| `swofty.json` | The manifest. `launcher` names the helper per platform, the `Overview` and `Example` sections, the `exmod` import extension, the build hook and the `example` command. |
| `src/main.rs` | The helper. It answers `hello`, `view`, `event`, `tick`, `import`, `build`, `command` and `summary` over stdin and stdout. |

The `Overview` page is a dashboard built from `dashboard.json` in the data folder: a hero with the game status, tiles, a settings form, meters and a driver table with sortable columns, a search box, editable ratings, an Active switch per driver, surnames in bold and a detail panel that opens when a driver is clicked. On launcher 1.5.25 or later the form also has a team colour field and the table a colour column. It ticks while open and invalidates itself when `dashboard.json` changes or the game starts or stops. The `Example` page lists the imported `.exmod` files with a switch each and a search box. The switches are per-profile state. The build hook writes the names of the items that are on to `enabled.txt` in the cache folder and installs it as `example-launcher-addon/enabled.txt`. `swofty example` prints each item and whether it is on.

## Build and install

```sh
cargo build --release
mkdir -p bin
cp target/release/example-helper bin/
swofty addons install .
swofty addons enable example.launcher-addon
```

On Windows, build `example-helper.exe` and copy it to `bin/`. Ship both binaries in `bin/` for players on both platforms.
