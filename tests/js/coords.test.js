// Pruebas de web/js/coords.js. Se ejecutan con: node tests/js/coords.test.js
const assert = require('assert');
const { parseCoordinates, formatCoordinates } = require('../../web/js/coords.js');

const cases = [
  ['23.1136, -82.3666', { lat: 23.1136, lon: -82.3666 }],
  ['23.1136,-82.3666', { lat: 23.1136, lon: -82.3666 }],
  ['  23.1136 -82.3666  ', { lat: 23.1136, lon: -82.3666 }],
  ['-34, 151', { lat: -34, lon: 151 }],
  ['https://www.google.com/maps/@23.1136,-82.3666,17z', { lat: 23.1136, lon: -82.3666 }],
  ['https://www.google.com/maps/place/Vedado/@23.1,-82.4,15z/data=!3m1!4b1!4m6!3m5!1s0x0!8m2!3d23.1136!4d-82.3666',
    { lat: 23.1136, lon: -82.3666 }],
  ['https://maps.google.com/?q=23.1136,-82.3666', { lat: 23.1136, lon: -82.3666 }],
  ['https://www.google.com/maps?ll=23.1136,-82.3666&z=15', { lat: 23.1136, lon: -82.3666 }],
  ['90, 180', { lat: 90, lon: 180 }],
];
for (const [input, expected] of cases) {
  assert.deepStrictEqual(parseCoordinates(input), expected, 'debía reconocer: ' + input);
}

const invalid = ['', '   ', 'hola', '23.1136', '95, 10', '10, 181', '23.1136, abc', 'https://example.com/', 'https://www.google.com/maps', null, undefined, 42, '1,2,3'];
for (const input of invalid) {
  assert.strictEqual(parseCoordinates(input), null, 'debía rechazar: ' + input);
}

assert.strictEqual(formatCoordinates(23.1136, -82.3666), '23.1136, -82.3666');
assert.strictEqual(formatCoordinates(null, null), '');
console.log('coords.test.js: ' + (cases.length + invalid.length) + ' casos correctos');
