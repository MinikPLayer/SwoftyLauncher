---
title: Offline play
description: Why modded sessions run single-player and offline without EA AntiCheat, the one-time consent, and how to play vanilla again.
---

# Offline play

When you play a modded session, F1 25 runs in single-player, offline. Multiplayer is off, and you can't turn it on. Launch through Steam without the launcher and you'll play vanilla.

The loader and addons need the game to run without EA AntiCheat, so the launcher disables it for each modded session. It undoes that change every time the game exits.

In an [OverlayFS session](/reference/launch#overlayfs-sessions) on Linux, the offline play stub is served from the common layer of the mount, and the real `EAAntiCheat.GameServiceLauncher.exe` is never moved or replaced. Nothing is copied into the game folder, so there is nothing to undo when the game exits.

## The one-time step

The first time you press **Play**, you'll see the **Modded play runs offline** dialog. It explains what you're accepting: modded sessions run offline, without EA AntiCheat, and EA's terms of service do not cover playing without EA AntiCheat even offline.

Select **Accept and launch** to start the game. If you decline, nothing changes and the game won't launch. You only need to accept once; after that, **Play** launches directly.

## Return to vanilla

The session undoes its changes when the game exits, so the game folder is vanilla again after you play. To play online:

1. Close the game.
2. On Linux, clear the launcher's Steam launch option.
3. Launch F1 25 through Steam.

If any files need repair, follow [Restoring the game](/using/profiles-and-restoring).

## Next

- [Launching the game](/reference/launch): how a session starts and where its logs go.
- [Restoring the game](/using/profiles-and-restoring): when to run `swofty restore`.
