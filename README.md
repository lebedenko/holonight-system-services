# HoloNight System Services

Reusable Qt/C++ system integration for HoloNight applications. The initial static component,
`HoloNightSystem::Audio`, owns the PulseAudio integration, controller, device and stream models, and value types.
Consumers retain presentation, QML registration, and application policy.
