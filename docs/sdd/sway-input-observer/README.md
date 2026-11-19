# Sway keyboard input observer

The Compositor package exports `HoloNight::System::SwayInputObserver`. A consumer
starts its own observer and watches `layoutNameChanged`. The accessor returns
the full Sway layout name or an empty string when unavailable.

The observer uses the shared Sway IPC frame encoder and decoder on a dedicated
connection. It subscribes to input events before requesting `GET_INPUTS` and
refreshes on keyboard layout, keymap, addition, and removal events. One request
may be in flight. Changes during that request cause one follow-up query, and
the observer discards the superseded response. On socket errors or request
timeout it clears the name and retries using the current `SWAYSOCK`.

Among keyboards reporting an active layout, the observer selects the one with
the most configured layout groups, then the lexicographically first identifier.
The consumer owns label formatting and presentation.

A fake local server verifies subscription and refresh order, keyboard
selection, coalescing, disconnect, and reconnect. The installed Compositor
consumer also compiles and links against the observer.
