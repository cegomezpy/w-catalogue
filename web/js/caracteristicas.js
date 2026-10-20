// Administración del catálogo de características (Ajustes > Características).
(function () {
  const CATEGORIES = { servicios: 'Servicios', espacios: 'Espacios', condicion: 'Condición', otra: 'Otra' };
  const TYPES = { bool: 'Sí / no', numero: 'Número', texto: 'Texto', opcion: 'Opción única', multiopcion: 'Opción múltiple' };
  // Subconjunto de Lucide incluido en web/vendor/icons/
  const ICONS = ['flame', 'zap', 'wifi', 'shield', 'shield-check', 'cctv', 'lock', 'key', 'droplets', 'droplet', 'snowflake',
    'fan', 'wind', 'thermometer', 'sun', 'trees', 'tree-palm', 'sprout', 'door-open', 'fence', 'bed-single', 'bed-double',
    'bath', 'washing-machine', 'refrigerator', 'tv', 'utensils', 'sofa', 'waves-ladder', 'car', 'house', 'building',
    'warehouse', 'mountain', 'umbrella', 'camera', 'sparkles', 'star', 'heart', 'map-pin', 'ruler', 'calendar'];

  function blank() {
    return {
      etiqueta: '', categoria: 'espacios', tipo: 'bool', opcionesText: '', unidad: '', icono: '',
      badge: { visible: true, texto: '', rol: 'secundario' }, activa: true,
    };
  }

  document.addEventListener('alpine:init', () => {
    Alpine.data('caracteristicasPage', () => ({
      icons: ICONS,
      items: [],
      editing: null,
      editingId: null,
      isNew: false,
      errors: {},
      banner: '',
      saving: false,
      pendingDelete: null,

      route(current, sub) {
        if (current === 'ajustes' && sub === 'caracteristicas') queueMicrotask(() => this.load());
      },

      async load() {
        try {
          this.items = await window.api.get('/caracteristicas');
        } catch (e) { /* notificado */ }
      },

      categoryLabel(c) { return CATEGORIES[c] || c; },
      typeLabel(t) { return TYPES[t] || t; },
      err(k) { return this.errors[k] || ''; },

      startNew() {
        this.editing = blank();
        this.editingId = null;
        this.isNew = true;
        this.errors = {};
        this.banner = '';
        this.$nextTick(() => document.getElementById('car-editor')?.scrollIntoView({ block: 'start' }));
      },

      startEdit(c) {
        this.editing = {
          etiqueta: c.etiqueta, categoria: c.categoria, tipo: c.tipo, opcionesText: c.opciones.join('\n'),
          unidad: c.unidad, icono: c.icono,
          badge: { visible: c.badge.visible, texto: c.badge.texto || '', rol: c.badge.rol }, activa: c.activa,
        };
        this.editingId = c.id;
        this.isNew = false;
        this.errors = {};
        this.banner = '';
        this.$nextTick(() => document.getElementById('car-editor')?.scrollIntoView({ block: 'start' }));
      },

      cancel() {
        this.editing = null;
        this.editingId = null;
        this.errors = {};
        this.banner = '';
      },

      // Texto por defecto del badge (placeholder del campo)
      defaultBadgeText() {
        const e = this.editing;
        if (!e) return '';
        return e.tipo === 'bool' ? (e.etiqueta || 'Etiqueta') : (e.etiqueta || 'Etiqueta') + ': {valor}';
      },

      sampleValue() {
        const e = this.editing;
        const opts = e.opcionesText.split('\n').map((s) => s.trim()).filter(Boolean);
        switch (e.tipo) {
          case 'bool': return 'Sí';
          case 'numero': return '2' + (e.unidad ? ' ' + e.unidad : '');
          case 'texto': return 'Ejemplo';
          case 'opcion': return opts[0] || 'Opción';
          default: return opts.slice(0, 2).join(', ') || 'Opción';
        }
      },

      previewText() {
        const e = this.editing;
        if (!e) return '';
        const sample = this.sampleValue();
        const tpl = e.badge.texto.trim();
        if (tpl) return tpl.split('{valor}').join(sample);
        return e.tipo === 'bool' ? (e.etiqueta || 'Etiqueta') : (e.etiqueta || 'Etiqueta') + ': ' + sample;
      },

      payload() {
        const e = this.editing;
        const p = {
          etiqueta: e.etiqueta, categoria: e.categoria, tipo: e.tipo, unidad: e.tipo === 'numero' ? e.unidad : '',
          icono: e.icono, activa: e.activa,
          badge: { visible: e.badge.visible, texto: e.badge.texto.trim() || null, rol: e.badge.rol },
        };
        p.opciones = (e.tipo === 'opcion' || e.tipo === 'multiopcion')
          ? e.opcionesText.split('\n').map((s) => s.trim()).filter(Boolean) : [];
        return p;
      },

      async save() {
        this.errors = {};
        this.banner = '';
        this.saving = true;
        try {
          if (this.isNew) await window.api.post('/caracteristicas', this.payload(), { silent: true });
          else await window.api.put('/caracteristicas/' + this.editingId, this.payload(), { silent: true });
          window.notify('Característica guardada', 'ok');
          this.cancel();
          await this.load();
        } catch (e) {
          if (e.fields && Object.keys(e.fields).length) this.errors = e.fields;
          else this.banner = e.message; // conflictos (409): tipo con valores, opción en uso...
          window.notify(Object.keys(this.errors).length ? 'Revise los campos marcados' : e.message, 'error');
        } finally {
          this.saving = false;
        }
      },

      async toggleActive(c) {
        try {
          await window.api.put('/caracteristicas/' + c.id, { activa: !c.activa });
          await this.load();
        } catch (e) { /* notificado */ }
      },

      async move(i, delta) {
        const ids = this.items.map((c) => c.id);
        const j = i + delta;
        if (j < 0 || j >= ids.length) return;
        [ids[i], ids[j]] = [ids[j], ids[i]];
        try {
          this.items = await window.api.put('/caracteristicas/orden', { ids });
        } catch (e) { /* notificado */ }
      },

      // Antes de confirmar se informa cuántas casas tienen un valor para la característica
      async askDelete(c) {
        try {
          const fresh = await window.api.get('/caracteristicas/' + c.id);
          this.pendingDelete = { item: c, count: fresh.casas_con_valor };
        } catch (e) { /* notificado */ }
      },

      async confirmDelete() {
        const id = this.pendingDelete.item.id;
        this.pendingDelete = null;
        try {
          await window.api.del('/caracteristicas/' + id);
          window.notify('Característica eliminada', 'ok');
          await this.load();
        } catch (e) { /* notificado */ }
      },
    }));
  });
})();
