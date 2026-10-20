// Router por hash: #/inicio, #/casas, #/casas/nueva, #/casas/12, #/ajustes/caracteristicas...
// `current` es la sección (primer segmento) y `sub` el resto de la ruta.
(function () {
  const SECTIONS = [
    { id: 'inicio', title: 'Inicio' },
    { id: 'casas', title: 'Casas' },
    { id: 'documentos', title: 'Documentos' },
    { id: 'ajustes', title: 'Ajustes' },
  ];
  const DEFAULT = 'inicio';

  function parse() {
    const parts = (location.hash || '').replace(/^#\/?/, '').split('/').filter(Boolean);
    const id = SECTIONS.some((s) => s.id === parts[0]) ? parts[0] : DEFAULT;
    return { current: id, sub: parts[0] === id ? parts.slice(1).join('/') : '' };
  }

  document.addEventListener('alpine:init', () => {
    const initial = parse();
    Alpine.store('router', {
      sections: SECTIONS,
      current: initial.current,
      sub: initial.sub,
      sync() {
        const r = parse();
        this.current = r.current;
        this.sub = r.sub;
      },
      go(path) {
        location.hash = '#/' + path;
      },
    });
    window.addEventListener('hashchange', () => Alpine.store('router').sync());
    if (!location.hash) location.hash = '#/' + DEFAULT;
  });

  // Recarga la hoja del tema (colores y fuentes) tras guardar ajustes o fuentes.
  window.reloadTheme = function () {
    const link = document.getElementById('tema-css');
    if (link) link.href = '/api/tema.css?v=' + Date.now();
  };
})();
