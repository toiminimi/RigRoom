# Privacy

RigRoom stores presets, settings, downloaded models, and plugin state locally.
TONE3000 requests are made only after a user provides an API key.

API keys are stored in the desktop Secret Service keyring when available. If
secure persistence is unavailable, the key is retained only for the current
application session. RigRoom does not transmit presets, plugin paths, or API
keys to the project.

Third-party plugins execute in-process and may have their own network, storage,
and privacy behavior. Install plugins only from sources you trust.
