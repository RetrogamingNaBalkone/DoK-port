# Offline QR verification

- `nayuki/qrcodegen.py`: Project Nayuki QR Code generator v1.8.0, MIT.
  https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/python
- `jsqr/jsQR.js`: jsQR 1.4.0, Apache-2.0.
  https://www.npmjs.com/package/jsqr/v/1.4.0

Unmodified upstream files copied from the repository's GBA vendor references.
NDS tools own these copies and do not import GBA runtime or tooling.
The frozen channel bitmap has the same URL as the GBA credits. Pixel checks
re-encode it with Nayuki; jsQR independently decodes the actual DS LCD callback.
