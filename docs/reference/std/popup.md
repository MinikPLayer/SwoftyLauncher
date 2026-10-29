---
title: Popup
description: Show the game's own message popup with your title and body from native code with swofty/popup.hpp.
---

# Popup

`swofty::popup::show(title, body)` raises the game's own message popup with your text and one OK button.

```cpp [native/src/my_plugin.cpp]
#include <cstring>

#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/popup.hpp>

namespace popup = swofty::popup;

void on_career_loaded(const SwoftyEvent *ev, void *)
{
    auto *s = static_cast<const SwoftyCareerState *>(ev->data);
    if (swofty::career::mode(*s) != swofty::career::Mode::my_team)
        return;
    // UTF-8; '\n' breaks a line
    popup::Result r = popup::show("My Addon", "My Addon works in Driver Career only.\nIts changes are off in this career.");
    if (r != popup::Result::ok)
        swofty::log("[my-addon] no popup: %s", popup::name(r));
}

SWOFTY_ON_LOAD
{
    if (!popup::available()) // false on a build where the core did not find the game's message system
        swofty::log("[my-addon] the popup is unavailable on this build");
    swofty::events::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_career_loaded);
}

SWOFTY_ADDON()
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-addon",
  "name": "My Addon",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.2.12" }],
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

```text [swofty/loader.log]
[popup] setup: live; construct base+0x42d9bf0, set attribute base+0x6a79c0, release base+0x42aa980, id vtable base+0x5613670
[popup] message system 00000004A9D4A330 (first message 'online_services_generic_exception_online_with_code')
[popup] queued by author.my-addon: "My Addon"
[popup] shown for author.my-addon as 'swofty_message': "My Addon"
```

The popup is the persistent multichoice popup the game uses for its errors and confirmations, drawn over the current screen in the game's style.

- **Header:** `swofty/popup.hpp`, namespace `swofty::popup`
- **Table:** `SwoftyApi::popup` (`SwoftyPopup`), API version 7, from core 0.2.12
- **Feature:** `popup`, stable and always on
- **Thread:** `show` from any thread; the popup opens on the flow thread

Every call that returns a `swofty::popup::Result` uses the shared [result codes](./#result-codes), and `name(r)` turns one into `"ok"`, `"unavailable"` and so on.

## available

```cpp
if (swofty::popup::available()) {
    // the core found the game's message system on this build
}
```

## show

```cpp
popup::show("Title", "Body");          // ok once the popup is queued
popup::show("", "Body only");          // ok: one of the two may be empty
popup::show("", "");                   // invalid: title and body are both empty
// unavailable: a core older than API 7, or the message system did not resolve on this build
```

- Text is UTF-8, and `\n` breaks a line. The core caps each string at 4000 bytes.
- A popup with the same title and body as one still waiting in the queue is not queued twice.
- The queue holds at most 8 popups. A ninth drops the oldest:

```text
[popup] queue full (8); dropping the oldest popup, from author.my-addon
```

- The popup closes when the player presses OK. Nothing reports the press back.
- A refused call is logged:

```text
[popup] show by author.my-addon refused: empty title and body
[popup] show by author.my-addon: unavailable (the message system did not resolve on this build)
```

Rnd Tyres Thermal checks `available()` to say in its own log whether the player will see the popup the core shows when it cancels a My Team career:

```cpp
// addons/rnd-tyres-thermal/native/src/rnd_myteam_block.cpp, trimmed
swofty::log("[rnd-tyres] My Team blocked: the core cancels starting or loading a My Team career and shows \"%s\"%s",
            kTitle,
            swofty::popup::available() ? "" : " (the popup is unavailable on this build; the route is still cancelled)");
```

## How it works

The game raises every popup the same way: it constructs a message by its id in the message data (`Ui_Message_Construct`), sets the message's user attributes through the returned handle (`Ui_Message_SetAttribute`) and drops the handle (`Ui_Message_ReleaseHandle`). The message system queues the message and the persistent screen draws it.

`show` queues the request. On the next flow tick the core constructs a `swofty_message`, sets its `title` and `message` attributes to your text and releases the handle.

```text
show("My Addon", "...")            queued by the core, any thread
  next flow tick                   Ui_Message_Construct("swofty_message")
                                   Ui_Message_SetAttribute(title = "My Addon", message = "...")
                                   Ui_Message_ReleaseHandle
  the persistent screen            draws the popup; OK closes it
```

The launcher's UI build adds `swofty_message` to `onlineservices_messages.uimsg`: title and body are `lng_value_tag` with a literal (`localise: false`) token that reads the `title` and `message` attributes, and one option, `lng_msg_ok`.

The core learns the message system's address from the first message the game constructs after boot (offline, the online-services error). A popup queued before that waits for it:

```text
[popup] queued by author.my-addon: "My Addon" (waiting for the game's first message to find the message system)
[popup] message system 00000004A9D4A330 (first message 'online_services_generic_exception_online_with_code')
```

Without `swofty_message` in the game's data, the core uses the stock `beta_motd` message, whose title is the fixed `lng_message_of_the_day_title`; your title then leads the body. The core log says which message it used:

```text
[popup] the game has no 'swofty_message' message (the launcher's UI build adds it); falling back to 'beta_motd', whose title is fixed, so the title leads the body
[popup] shown for author.my-addon as 'beta_motd': "My Addon"
```

## Availability

The core finds the message functions through one signature over the game's `beta_motd` poster and one over the handle release. On a build where either does not match, `available()` returns `false` and `show` returns `unavailable`. The core log's `[popup] setup:` line says what resolved:

```text
[popup] setup: live; construct base+0x42cdb50, set attribute base+0x6a83d0, release base+0x429e5f0, id vtable base+0x5725ad0
[popup] message system: Ui_Message_PostMotd signature matched 0 time(s)
[popup] the message system did not resolve (poster missing, release missing, constructor missing); popup.show reports unavailable
```

Requires `swofty.core` 0.2.12 or later:

```json [swofty.json]
"requires": [{ "id": "swofty.core", "version": ">=0.2.12" }]
```
