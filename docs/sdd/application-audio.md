# Audio application identity contract

AudioStream appends application_id and process_binary from PulseAudio's application.id
and application.process.binary properties for playback and recording streams. Missing
properties produce empty strings. AudioStreamModel appends applicationId and
processBinary roles after currentDevice; existing role numbers and per-stream commands
are unchanged. Add/change notifications remain upserts by stream ID.

The provider owns raw streams, without application grouping or QML presentation policy.
An unavailable backend clears both raw stream models, including repeated unavailable
notifications, so consumers cannot retain stale membership across reconnects.

Acceptance: focused audio backend, controller and model tests, followed by the audio
install-tree consumer test. Build with host dependencies and install to a private
development prefix before building consumers.
