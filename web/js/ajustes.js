// Página de Ajustes: tema, tipografía (biblioteca de fuentes), contacto y moneda.
(function () {
  const SAMPLE = 'Casa amplia con jardín — Peña, ñandú, acción';
  const WEIGHTS = [100, 200, 300, 400, 500, 600, 700, 800, 900];
  const registered = new Set();

  // Registra cada variante con la API FontFace para verla al instante, sin recargar.
  function registerFont(family, v) {
    const alias = 'wc-lib ' + family;
    const key = alias + '|' + v.peso + '|' + v.estilo + '|' + v.archivo;
    if (registered.has(key)) return;
    registered.add(key);
    const face = new FontFace(alias, 'url("' + v.url + '?v=' + Date.now() + '")', {
      weight: String(v.peso),
      style: v.estilo,
    });
    face.load().then((f) => document.fonts.add(f)).catch(() => registered.delete(key));
  }

  function emptyForm() {
    return {
      tema: { colorPrimario: '', colorSecundario: '', colorTerciario: '' },
      tipografia: { fuenteTitulos: '', fuenteTexto: '' },
      contacto: { nombre: '', telefono: '', whatsapp: '', email: '', web: '', instagram: '', facebook: '' },
      general: { moneda: '' },
    };
  }

  document.addEventListener('alpine:init', () => {
    Alpine.data('ajustesPage', () => ({
      sample: SAMPLE,
      weights: WEIGHTS,
      form: emptyForm(),
      errors: {},
      saving: false,
      loaded: false,
      families: [],
      pendingDelete: null,

      // Importación en dos pasos
      imp: { step: 0, busy: false, meta: null, error: '', askReplace: false },

      async init() {
        await this.load();
      },

      async load() {
        const [settings, families] = await Promise.all([
          window.api.get('/ajustes'),
          window.api.get('/fuentes'),
        ]);
        // primero las opciones de los selectores y después el valor, para que el select lo muestre
        this.setFamilies(families);
        await this.$nextTick();
        this.form = settings;
        this.loaded = true;
      },

      setFamilies(list) {
        this.families = list;
        list.forEach((f) => f.variantes.forEach((v) => registerFont(f.familia, v)));
      },

      async reloadFamilies() {
        this.setFamilies(await window.api.get('/fuentes'));
      },

      err(key) {
        return this.errors[key] || '';
      },

      familyStyle(family, weight) {
        return { fontFamily: "'wc-lib " + family + "', sans-serif", fontWeight: weight || 400 };
      },

      async save() {
        this.saving = true;
        this.errors = {};
        try {
          this.form = await window.api.put('/ajustes', this.form, { silent: true });
          window.reloadTheme(); // recarga tema.css (vista previa y colores de los badges)
          window.notify('Ajustes guardados', 'ok');
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

      // ---- fuentes ----
      resetImport() {
        this.imp = { step: 0, busy: false, meta: null, error: '', askReplace: false };
        if (this.$refs.fontFile) this.$refs.fontFile.value = '';
      },

      async analyze(ev) {
        const file = ev.target.files[0];
        if (!file) return;
        this.imp = { step: 0, busy: true, meta: null, error: '', askReplace: false };
        const fd = new FormData();
        fd.append('archivo', file, file.name);
        try {
          const meta = await window.api.upload('/fuentes/analizar', fd, { silent: true });
          this.imp = { step: 2, busy: false, meta, error: '', askReplace: false };
        } catch (e) {
          this.imp = { step: 0, busy: false, meta: null, error: e.message, askReplace: false };
          if (this.$refs.fontFile) this.$refs.fontFile.value = '';
        }
      },

      async importFont(replace) {
        const m = this.imp.meta;
        this.imp.busy = true;
        this.imp.error = '';
        try {
          await window.api.post(
            '/fuentes',
            { token: m.token, familia: m.familia, peso: Number(m.peso), estilo: m.estilo, reemplazar: !!replace },
            { silent: true }
          );
          await this.reloadFamilies();
          window.reloadTheme();
          window.notify('Fuente importada: ' + m.familia, 'ok');
          this.resetImport();
        } catch (e) {
          this.imp.busy = false;
          if (e.status === 409 && e.message.indexOf('reemplazar') !== -1) {
            this.imp.askReplace = true; // la variante ya existe: pide confirmar
          } else {
            this.imp.error = e.message;
          }
        }
      },

      confirmDelete(v, familia) {
        this.pendingDelete = { id: v.id, label: familia + ' ' + v.peso + ' ' + (v.estilo === 'italic' ? 'cursiva' : 'normal') };
      },

      async deleteFont() {
        const id = this.pendingDelete.id;
        this.pendingDelete = null;
        try {
          const res = await window.api.del('/fuentes/' + id);
          (res.avisos || []).forEach((a) => window.notify(a, 'info'));
          await this.load(); // los ajustes pueden haber vuelto a la fuente por defecto
          window.reloadTheme();
        } catch (e) { /* api.js ya mostró la notificación */ }
      },

      async openGoogleFonts() {
        try {
          await window.api.post('/sistema/abrir-enlace', { url: 'https://fonts.google.com' });
        } catch (e) { /* notificado */ }
      },
    }));
  });
})();
