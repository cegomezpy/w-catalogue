// Interpreta coordenadas pegadas por el usuario. Formatos admitidos:
//   "23.1136, -82.3666"                       (lat, lon, como los copia Google Maps)
//   "23.1136 -82.3666"
//   "https://www.google.com/maps/@23.1136,-82.3666,17z"
//   "https://www.google.com/maps/place/Algo/@23.1136,-82.3666,17z/data=..."
//   "https://www.google.com/maps/place/.../data=!3d23.1136!4d-82.3666"
//   "https://maps.google.com/?q=23.1136,-82.3666"
// Devuelve {lat, lon} o null si no se reconoce o está fuera de rango.
(function (root) {
  const NUM = '[-+]?\\d+(?:\\.\\d+)?';

  function inRange(lat, lon) {
    return Number.isFinite(lat) && Number.isFinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
  }

  function parseCoordinates(text) {
    if (typeof text !== 'string') return null;
    const s = text.trim();
    if (!s) return null;

    let m;
    if (/^https?:\/\//i.test(s)) {
      // la ubicación del marcador (!3d..!4d..) es más precisa que el centro del mapa (@..)
      m = s.match(new RegExp('!3d(' + NUM + ')!4d(' + NUM + ')')) ||
          s.match(new RegExp('@(' + NUM + '),(' + NUM + ')')) ||
          s.match(new RegExp('[?&](?:q|ll|query)=(' + NUM + '),\\s*(' + NUM + ')'));
    } else {
      m = s.match(new RegExp('^(' + NUM + ')\\s*[,;]?\\s+(' + NUM + ')$')) ||
          s.match(new RegExp('^(' + NUM + ')\\s*[,;]\\s*(' + NUM + ')$'));
    }
    if (!m) return null;
    const lat = parseFloat(m[1]);
    const lon = parseFloat(m[2]);
    return inRange(lat, lon) ? { lat, lon } : null;
  }

  function formatCoordinates(lat, lon) {
    return lat == null || lon == null ? '' : lat + ', ' + lon;
  }

  root.parseCoordinates = parseCoordinates;
  root.formatCoordinates = formatCoordinates;
  if (typeof module !== 'undefined' && module.exports) module.exports = { parseCoordinates, formatCoordinates };
})(typeof window !== 'undefined' ? window : globalThis);
