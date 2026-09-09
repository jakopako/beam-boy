# Beam Boy CA bundle

`x509_crt_bundle.bin` is an ESP32 `WiFiClientSecure` certificate bundle generated
from Mozilla's public CA set as packaged by Python `certifi`.

It is embedded into firmware via `board_build.embed_files` in `platformio.ini`
and used by the Phase 7 Store scene to validate the HTTPS `index.json` host
(GitHub Pages or another normal public HTTPS endpoint). It is deliberately kept
outside `data/`, so it is not copied into the LittleFS image.

Regenerate after updating `certifi` with a script equivalent to:

```powershell
python -m pip install --upgrade certifi cryptography
```

Then rebuild the bundle as subject/public-key records sorted by subject name, as
expected by Arduino ESP32's `esp_crt_bundle.c`.

