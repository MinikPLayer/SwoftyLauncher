# Career Mode Editor protocol

The launcher page and the in-game plugin talk through files in `<game>/swofty/addons/swofty.career-editor/`, or in `$DATA/addon-storage/swofty.career-editor/` in a session where the game can't create files in its folder (the folder `addon_game_dir` names). Each area documents its ops and its snapshot section in its own file.

| area | file | owner |
|---|---|---|
| Mailbox, snapshot, status, journal | [protocol/bridge.md](protocol/bridge.md) | bridge |
| Economy | [protocol/economy.md](protocol/economy.md) | economy |
| R&D | [protocol/rnd.md](protocol/rnd.md) | R&D |
| Facilities | [protocol/facilities.md](protocol/facilities.md) | R&D |
| Drivers | [protocol/drivers.md](protocol/drivers.md) | drivers |
| Season | [protocol/season.md](protocol/season.md) | season |
| Weather | [protocol/weather.md](protocol/weather.md) | weather |
