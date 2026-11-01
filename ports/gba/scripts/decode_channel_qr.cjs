// Independent decoder of a 240x160 emulator RGBA frame.
const fs = require('fs');
const jsQR = require('./vendor/jsqr/jsQR.js');
const pixels = Uint8ClampedArray.from(fs.readFileSync(process.argv[2]));
const result = jsQR(pixels, 240, 160, { inversionAttempts: 'dontInvert' });
if (!result) throw new Error('No QR code found in the mGBA frame');
process.stdout.write(JSON.stringify({data: result.data, version: result.version}));
