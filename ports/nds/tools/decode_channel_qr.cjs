// Decode the actual 256x192 lower DS LCD, RGBA8.
const fs = require('fs');
const jsQR = require('./vendor/jsqr/jsQR.js');
const pixels = Uint8ClampedArray.from(fs.readFileSync(process.argv[2]));
const result = jsQR(pixels, 256, 192, { inversionAttempts: 'dontInvert' });
if (!result) throw new Error('No QR code found in the DS LCD');
process.stdout.write(JSON.stringify({data: result.data, version: result.version}));
