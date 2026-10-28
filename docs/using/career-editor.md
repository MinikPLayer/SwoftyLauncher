---
title: Career Mode Editor
description: Edit a running F1 25 career from the launcher, in My Team and Driver Career.
---

# Career Mode Editor

The Career Mode Editor addon adds a **Career Editor** page to the launcher. It edits the career that is loaded in F1 25 while the game runs, in My Team and in Driver Career. It needs Swofty Launcher for F1 25 1.0.10 or later.

## Install

1. Download [`swofty.career-editor.zip`](https://github.com/Swofty-Developments/SwoftyLauncher/releases/latest/download/swofty.career-editor.zip) from the latest release.
2. Drag it onto the launcher window, or follow [Install an addon](/using/addons#install-an-addon).
3. Turn it on in your profile and start the game.

## Use it

Load a career and go to Team HQ. The **Career Editor** page shows the game, the career mode, the season and round, and whether edits can apply now. Its tabs:

| Tab | Edits |
|---|---|
| Overview | Status, recent edits and backups |
| Team | Cash, resource points, renown, sponsor loyalty, cost cap refunds and facilities (My Team); the driver's resource points (Driver Career) |
| Drivers | Ratings, seat swaps and contract money. Swap Seats moves free agents into race seats in Driver Career only, and there a driver who takes a seat signs that seat's contract; Contracts lists race drivers without a contract for their team and signs them |
| R&D | Complete, fit, unlock, refund or reset upgrades |
| Season | Calendar rounds and points adjustments |
| Weather | Weather for each session of a weekend, and the weather seed |
| History | Every edit with its before and after values, undo, and save backups |

## Calendar

**Season** lists the calendar with each round's track and country, and changes rounds after the current one that haven't started: move a round or replace its track. **Move a round** shows what the move does before you apply it, such as "Suzuka becomes round 9, Monaco moves to round 10". With Swofty Launcher 1.0.12 or later, **Add a round** and **Replace track** list every Grand Prix track in your profile's last build, in three groups:

| Group | Tracks |
|---|---|
| In this championship | Tracks from a removed round or another season of this career |
| Other tracks | The game's other Grand Prix tracks |
| Modded tracks | Community tracks turned on in [Mods and tracks](/using/mods#tracks), such as Okayama or Sepang |

To race a modded track, import its zip on the **Mods** page, turn it on in the profile and press **Play** to build; it then shows under **Modded tracks**. A track already on this season's calendar isn't listed again. A track turned on after the last build appears once the profile builds again; **Play** builds it. Under the forms, the page names each track it doesn't offer and why, such as a track whose mod is off. An older launcher lists only tracks this championship already has.

The number of rounds stays as the season started. The game keeps its own list of race weekends for the season, set by the season length, and the career moves through those dates; the season ends after its last race weekend. **Add a round** and **Remove a round** are open only on a season whose round count no longer matches that list.

A season edited with Career Mode Editor 0.3.4 or older can have more or fewer rounds than race weekends. After its last round the game then goes to a race weekend that no longer exists and crashes, or the season can't finish. **Season** then shows **Round count** with **Repair the round count**:

| The calendar has | Repair |
|---|---|
| Fewer rounds than race weekends | Adds the missing rounds at the end, with tracks of this career's other seasons that aren't on this calendar. Replace them afterwards with **Replace track**. |
| More rounds than race weekends | Removes the last rounds that haven't started |

The repair has no undo; **History** restores a save backup.

## Your own ratings

In Driver Career your own ratings include your career progress. The editor sets the rating you ask for, and your progress keeps adding to it as the career goes on. Each of your ratings has a lowest value the editor can set, and it rises with your progress. A lower value is refused, and the message names the lowest one.

## Race numbers

**Drivers** sets a driver's race number, from 1 to 99, yours included: click the number in the ratings table, or use **Race numbers**. Each number belongs to one driver in the championship. To take a number another driver has, turn on the swap in **Race numbers**; he gets the old number. The number is saved with the career.

A free agent without a number who takes a seat in **Swap Seats** gets a free one, as the game's own signings give, and the driver he replaces keeps his. Undoing the swap takes the new number back.

## Driver Manager drivers in Driver Career

A [Driver Manager](/using/driver-manager) driver made after a Driver Career started has no place in that career's line-up, so **Swap Seats** doesn't list him. In Driver Career, **Swap Seats** lists these drivers under **Add to line-up as free agent**. Adding one gives him a line-up place as a free agent, with his Driver Manager look, no team, no car and no race number, and an empty contract, the way the game sets up the free agents of a new career. The career keeps him when it saves. He then shows in the driver lists, and **Swap Seats** can put him in a race seat. The edit has no undo; **History** restores a save backup. It's experimental. Back up your save first.

## Edits

Edits queue while a session is loading, time is advancing or the game is in a menu transition, and apply when Team HQ is idle. Weather edits apply at any time in a loaded career. Screens that are open when an edit applies show it the next time they open.

The game saves the edited career at its next save. The editor backs up your saves before the first edit of each game session; **History** restores a backup while the game is closed.

Edits marked **Unsafe** write a value directly instead of through the game's own path. Contract money is one.

Edits marked **Experimental** are not fully tested in the game. In My Team, **Free agent into a seat** is one: a free agent takes an AI or team-mate race seat now, and takes over that driver's contract records with the team. In Driver Career, **Add to line-up as free agent** is another. Back up your save before you use either.
