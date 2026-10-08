# Firmware manifest metadata

Own authenticated `GET /openquatt/firmware/metadata` endpoint for the successful
manifest-publication counter. The update callback records successful manifests,
including unchanged versions; OTA progress, errors and aborts do not increment it.
This replaces the local `web_server` update-JSON patch.

The counter is atomic across main-loop publications and HTTPD reads. The boot ID
is immutable before listener startup. Responses use a bounded 80-byte payload,
without a JSON arena, entity, persistent record or copy of mutable update data.

Manual checks read metadata before issuing the command, then bracket each entity
refresh with metadata reads. They only confirm a changed revision in the same
boot, with a coherent refresh and unchanged channel/target selection. Missing or
invalid metadata fails closed; revision rollback or reboot cannot confirm a check.
