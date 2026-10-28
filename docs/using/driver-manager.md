---
title: Driver Manager
description: Create new F1 25 drivers in the launcher and sign them as free agents in new careers.
---

# Driver Manager

The Driver Manager addon adds a **Driver Manager** page to the launcher. It creates new drivers, each with its own id and its own rows in the game database. New drivers are free agents in careers started after they are added. It needs Swofty Launcher for F1 25 1.0.12 or later.

## Install

1. Download `swofty.driver-manager.zip` from the [1.0.12 release](https://github.com/Swofty-Developments/SwoftyLauncher/releases/tag/v1.0.12).
2. Drag it onto the launcher window, or follow [Install an addon](/using/addons#install-an-addon).
3. Turn it on in your profile.

## Create a driver

Open **Driver Manager** and press **Add driver**. Set:

| Field | Values |
|---|---|
| First and last name | Up to 24 characters each. **Surname first** shows the name as "LAST First". |
| Code | 3 letters, A to Z, unique among your drivers |
| Number | 1 to 99 |
| Nationality, head, helmet, suit, gloves, pose, podium emote | The game's own choices, with previews made from your copy of the game. Heads are generic, or look like a real driver. Heads and kit that aren't installed aren't offered. A head or kit that an addon in the profile installs, such as one from More Driver Models, is offered with the addon's name after it, and so is a head an addon adds to the game, such as an imported [face mod](/using/mods#face-mods). |
| Commentary name | The surname the commentators say, or none |
| Ratings | Experience, racecraft, awareness, pace and focus, each with a start and a peak value from 0 to 100. The game sets the starting focus itself and moves it with form; the focus values are only a guide. |
| F1 seasons completed, garage slot preference | As on the game's own drivers |
| Birth date | Year, month and day |
| AI traits | Aggression, car control, mistake avoidance, reaction speed, corner entry speed and corner exit speed, from 0 to 1. Higher is stronger; stock drivers sit between 0.2 and 1. |

Press **Add driver** to save. The page lists every driver with code, number, nationality, status and check result. **Edit**, **Duplicate**, **Retire** and **Restore** act on one driver. Every profile with Driver Manager on gets the same drivers.

## Set a value for every driver

**Set for every driver** gives one rating or AI trait the same value on each of your drivers. Choose the rating, a value from 0 to 100 and **Every driver** or **Free agents only**, then press **Set for drivers**. The value starts at 100, the most. For AI traits 100 is the game's 1.0; for mistake avoidance it means the fewest mistakes. Ratings take the start, the peak or both. A start above a driver's peak raises the peak to match, and a peak below the start lowers the start. The game's own drivers aren't changed.

## Play with your drivers

The next launch adds the drivers to the game. Start a new career to find them among the free agents. Driver Manager builds after your other addons and mods, so the drivers join the driver pools a team mod leaves. The [Career Mode Editor](/using/career-editor) swaps a free agent into a race seat in Driver Career. In My Team its experimental **Free agent into a seat** card places one.

## Game drivers

**Game drivers** lists the drivers the game already has. **Edit** on one sets its head and helmet from the same lists, including heads and helmets addons install. The helmet goes on every look the driver has, one per season and series. **Use the stock head and helmet** undoes the change. The changes apply the next time you play with Driver Manager on, in every profile with Driver Manager on. A head or helmet that isn't installed when you play leaves the driver's stock one in place.

## In the DB Editor

The [DB Editor](/using/db-editor#driver-manager-drivers) lists your drivers in the choices of its driver and entrant fields, such as a seat's **Entrant** on **Line-ups**, as the name and **(Driver Manager)**. Retired drivers show as **(Driver Manager, retired)**; drivers marked **left out** aren't listed, apart from those left out for a head or kit that isn't installed. Its search finds them by name, code and label: `dm_` and the driver id, such as `dm_208`. The drivers are edited here, not in the DB Editor.

A DB Editor addon that picks one of your drivers needs Driver Manager on in the profile. When Driver Manager is off, or leaves the driver out, the launch log names the addon and the driver.

## Checks

A driver with an error is marked **left out** and isn't added to the game. Errors include an empty name, a code that isn't 3 letters or is already used, a number outside 1 to 99, a choice the game doesn't have or hasn't installed, a date that doesn't exist and a peak below its start. Warnings, such as a number another driver also uses, don't stop the driver being added.

## Existing careers

- Drivers appear only in careers created after they are added.
- Ratings apply to new careers. A career keeps its own copy of every driver's ratings.
- Name, number, nationality, head and commentary name changes reach careers that already have the driver.
- **Retire** removes a driver from new careers and keeps careers that have the driver loading.
- Turning Driver Manager off or deleting it breaks careers that contain its drivers. The launcher asks before it does either.
