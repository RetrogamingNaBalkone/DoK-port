# Offline QR tools

Copied unchanged from the user's `C:/Source/Wonderfield-gba/scripts/vendor/`.

- `nayuki/qrcodegen.py`: Project Nayuki QR Code generator v1.8.0, MIT.
  https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/python
  Used by `ui_copy.py`; runtime stores only the generated 106-byte module mask.
- `jsqr/jsQR.js`: jsqr 1.4.0 distribution, Apache-2.0.
  https://www.npmjs.com/package/jsqr/v/1.4.0
  Used by Node.js only to independently decode emulator frames during verification.

Licenses are alongside each library. SHA-256 of unchanged upstream files:

```text
b089855caf16185c61421ea4927c1b213cf9468940d71fa8ab11ef83662dcc84  nayuki/qrcodegen.py
bc40c8a15196236b2314db0856f72ca0b49980cd5413b8c852a7349f5fee0859  jsqr/jsQR.js
```
