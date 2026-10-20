// Lista de casas y formulario (secciones Básico, Fotos, Precio, Distribución, Ubicación y Características).
(function () {
  const ESTADOS = { disponible: 'Disponible', reservada: 'Reservada', vendida: 'Vendida', alquilada: 'Alquilada' };
  const CATEGORIAS = { servicios: 'Servicios', espacios: 'Espacios', condicion: 'Condición', otra: 'Otras' };
  const CAMPOS_TEXTO = ['titulo', 'descripcion', 'operacion', 'estado', 'precio', 'moneda', 'sin_precio',
    'cuartos', 'banos', 'comedores_independientes', 'niveles', 'area_construida_m2', 'area_terreno_m2',
    'direccion', 'barrio', 'municipio', 'provincia'];

  function blankForm(currency) {
    const f = {};
    CAMPOS_TEXTO.forEach((k) => { f[k] = ''; });
    f.estado = 'disponible';
    f.sin_precio = 'consultar';
    f.moneda = currency || 'USD';
    return f;
  }

  function formatPrice(h) {
    if (h.precio == null) return h.sin_precio === 'consultar' ? 'Consultar' : '—';
    try {
      return new Intl.NumberFormat('es', { style: 'currency', currency: h.moneda || 'USD' }).format(h.precio);
    } catch (e) {
      return h.precio + ' ' + (h.moneda || '');
    }
  }

  function formatDate(iso) {
    const d = new Date(iso);
    return isNaN(d) ? '' : d.toLocaleDateString('es', { day: 'numeric', month: 'short', year: 'numeric' });
  }

  document.addEventListener('alpine:init', () => {
    Alpine.data('casasPage', () => ({
      ...window.fotosMixin(),
      estados: ESTADOS,
      categorias: CATEGORIAS,
      view: 'list', // list | form
      // lista
      houses: [],
      q: '',
      estadoFiltro: '',
      loading: false,
      searchTimer: null,
      listLoaded: false,
      // formulario
      id: null,
      f: blankForm(),
      coordsText: '',
      coordsError: '',
      catalog: [],
      values: {},
      badges: [],
      errors: {},
      saving: false,
      confirmingDelete: false,
      defaultCurrency: 'USD',
      badgeTimer: null,

      // El router cambia: #/casas, #/casas/nueva o #/casas/{id}
      route(current, sub) {
        if (current !== 'casas') return;
        queueMicrotask(() => this.applyRoute(sub));
      },

      async applyRoute(sub) {
        if (!sub) {
          this.view = 'list';
          await this.loadList();
        } else if (sub === 'nueva') {
          await this.openForm(null);
        } else if (/^\d+$/.test(sub)) {
          await this.openForm(Number(sub));
        } else {
          window.location.hash = '#/casas';
        }
      },

      // ---- lista ----
      async loadList() {
        this.loading = true;
        try {
          const params = new URLSearchParams();
          if (this.q.trim()) params.set('q', this.q.trim());
          if (this.estadoFiltro) params.set('estado', this.estadoFiltro);
          const qs = params.toString();
          this.houses = await window.api.get('/casas' + (qs ? '?' + qs : ''));
          this.listLoaded = true;
        } catch (e) { /* notificado por api.js */ } finally {
          this.loading = false;
        }
      },

      searchChanged() {
        clearTimeout(this.searchTimer);
        this.searchTimer = setTimeout(() => this.loadList(), 250); // debounce
      },

      price: formatPrice,
      date: formatDate,
      estadoLabel(e) { return ESTADOS[e] || e; },
      place(h) { return [h.barrio, h.municipio].filter(Boolean).join(', '); },

      // ---- formulario ----
      async openForm(id) {
        this.view = 'form';
        this.id = id;
        this.errors = {};
        this.confirmingDelete = false;
        this.coordsError = '';
        const [settings, features] = await Promise.all([
          window.api.get('/ajustes'),
          window.api.get('/caracteristicas?activas=1'),
        ]);
        this.defaultCurrency = settings.general.moneda || 'USD';
        this.catalog = features;
        // las opciones de los selectores se dibujan antes de asignarles valor (si no, el select no lo muestra)
        await this.$nextTick();
        await this.$nextTick();
        this.f = blankForm(this.defaultCurrency);
        this.coordsText = '';
        this.values = {};
        features.forEach((c) => { this.values[c.id] = this.emptyValue(c); });
        this.badges = [];
        this.setPhotos([]);

        if (id != null) {
          try {
            const h = await window.api.get('/casas/' + id);
            CAMPOS_TEXTO.forEach((k) => { this.f[k] = h[k] == null ? '' : h[k]; });
            if (h.precio == null) this.f.moneda = this.defaultCurrency;
            this.coordsText = window.formatCoordinates(h.lat, h.lon);
            h.caracteristicas.forEach((c) => {
              if (c.caracteristica_id in this.values) this.values[c.caracteristica_id] = this.fromApi(c.caracteristica_id, c.valor);
            });
            this.badges = h.badges;
            this.setPhotos(h.fotos);
          } catch (e) {
            window.location.hash = '#/casas';
            return;
          }
        }
        this.refreshBadges();
      },

      emptyValue(c) {
        if (c.tipo === 'bool') return false;
        if (c.tipo === 'multiopcion') return [];
        return '';
      },

      fromApi(id, v) {
        const c = this.catalog.find((x) => x.id === id);
        if (c.tipo === 'numero') return String(v);
        return v;
      },

      // Cambios en las coordenadas pegadas: se interpretan al salir del campo
      parseCoords() {
        this.coordsError = '';
        const text = this.coordsText.trim();
        if (!text) return;
        const c = window.parseCoordinates(text);
        if (!c) {
          this.coordsError = 'No se reconocen las coordenadas. Use "lat, lon" (por ejemplo 23.1136, -82.3666) o pegue un enlace de Google Maps.';
          return;
        }
        this.coordsText = window.formatCoordinates(c.lat, c.lon);
      },

      byCategory() {
        const groups = [];
        Object.keys(CATEGORIAS).forEach((cat) => {
          const items = this.catalog.filter((c) => c.categoria === cat);
          if (items.length) groups.push({ key: cat, label: CATEGORIAS[cat], items });
        });
        return groups;
      },

      toggleMulti(id, option) {
        const arr = this.values[id];
        const i = arr.indexOf(option);
        if (i === -1) arr.push(option);
        else arr.splice(i, 1);
        this.refreshBadges();
      },

      // valores informados, en el formato de la API (lo vacío se omite)
      featurePayload() {
        const out = [];
        this.catalog.forEach((c) => {
          const v = this.values[c.id];
          if (c.tipo === 'bool') { if (v) out.push({ caracteristica_id: c.id, valor: true }); return; }
          if (c.tipo === 'multiopcion') { if (v.length) out.push({ caracteristica_id: c.id, valor: v }); return; }
          if (String(v).trim() !== '') out.push({ caracteristica_id: c.id, valor: v });
        });
        return out;
      },

      refreshBadges() {
        clearTimeout(this.badgeTimer);
        this.badgeTimer = setTimeout(async () => {
          try {
            const res = await window.api.post('/casas/previsualizar-badges', { caracteristicas: this.featurePayload() }, { silent: true });
            this.badges = res.badges;
            delete this.errors['preview'];
          } catch (e) {
            this.badges = []; // un valor a medias (p. ej. un número inválido) no muestra vista previa
          }
        }, 200);
      },

      err(key) { return this.errors[key] || ''; },

      buildPayload() {
        const p = {};
        CAMPOS_TEXTO.forEach((k) => { p[k] = this.f[k]; });
        const c = window.parseCoordinates(this.coordsText);
        p.lat = c ? c.lat : null;
        p.lon = c ? c.lon : null;
        p.caracteristicas = this.featurePayload();
        return p;
      },

      async save() {
        this.errors = {};
        if (this.coordsText.trim() && !window.parseCoordinates(this.coordsText)) {
          this.parseCoords();
          this.errors = { coordenadas: this.coordsError };
          window.notify('Revise los campos marcados', 'error');
          return;
        }
        this.saving = true;
        try {
          const payload = this.buildPayload();
          if (this.id == null) await window.api.post('/casas', payload, { silent: true });
          else await window.api.put('/casas/' + this.id, payload, { silent: true });
          window.notify('Casa guardada', 'ok');
          window.location.hash = '#/casas';
        } catch (e) {
          if (e.fields && Object.keys(e.fields).length) {
            this.errors = e.fields;
            window.notify('Revise los campos marcados', 'error');
          } else {
            window.notify(e.message, 'error');
          }
        } finally {
          this.saving = false;
        }
      },

      async remove() {
        this.confirmingDelete = false;
        try {
          await window.api.del('/casas/' + this.id);
          window.notify('Casa eliminada', 'ok');
          window.location.hash = '#/casas';
        } catch (e) { /* notificado */ }
      },
    }));
  });
})();
