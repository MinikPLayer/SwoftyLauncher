# Any Team Transfers

In a Driver Career contract negotiation, every team makes you an offer. The negotiation's Team Select shows every team as available, and you can sign for any of them. Needs Swofty Launcher for F1 25 1.5.13 or later.

## Playing it

Turn the addon on and play a Driver Career as usual. When your contract ends and the contract event opens Team Select, every team on the grid can be picked, your current team included. The contract terms, the signing and the move at the end of the season are the game's own.

Without the addon, the game offers you two or three teams ranked close to your current one, picked at random, plus your current team when it renews you.

## With Progressive Career

A career with **Follow your driver's real career** still signs only for your driver's real team. **Choose your team** careers get every team.

## How it works

`native/any_team.dll` wraps `Game_DriverContracts_BuildOffers`, the function that builds a driver's offers before Team Select lists the teams. Team Select lists the teams of the game mode's entrant list and enables a team only when it is in your offers.

- After the game builds its offers, the wrap adds every other team of that same entrant list, using the offer list's own allocator.
- Your current team is left out after you start talks with another team, as in the stock game.
- The wrap runs inside every other addon's wrap of the same function (priority -10).
- Before its first call, the wrap checks that `Game_DriverContracts_BuildOffers` and `Game_CareerSetup_ResetEntrants` still read and write those fields where the addon expects them. The Steam and EA builds both match. On a build that doesn't, offers stay stock and `swofty/loader.log` says why.

Each negotiation logs its offers: `[any-team] driver N: stock offers ...; added ... team(s); offers now ...`.

## Layout

| Path | What |
|---|---|
| `native/` | The in-game plugin. Build with `native/build.sh`; host tests run with `native/test/run.sh`, optionally with `--build-offers <dump>` and `--reset-entrants <dump>` of the real functions. |
| `icons/` | The addon icon and its SVG source. |

Package with `tools/pack_addon.py --zip addons/any-team-transfers <out>`.
