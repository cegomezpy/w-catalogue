// Documentos: lista, creación de flyers y catálogos, editor con vista previa por formato y panel Exportar.
(function () {
  const TIPOS = { flyer: 'Flyer', catalogo: 'Catálogo' };
  const MOTIVOS_PDF = {
    edge_no_encontrado: 'No se encontró Microsoft Edge en este equipo.',
    tiempo_agotado: 'Edge tardó demasiado en generar el PDF.',
    fallo_edge: 'Edge no pudo generar el PDF.',
  };

  function formatDate(iso) {
    const d = new Date(iso);
    return isNaN(d) ? '' : d.toLocaleDateString('es', { day: 'numeric', month: 'short', year: 'numeric' });
  }

  document.addEventListener('alpine:init', () => {
    Alpine.data('documentosPage', () => ({
      view: 'list', // list | editor
      // lista
      docs: [],
      houses: [],
      listLoaded: false,
      creating: null, // null | 'flyer' | 'catalogo'
      newFlyerHouse: '',
      newCatTitle: '',
      newCatHouses: {},
      busy: false,
      // editor
      id: null,
      doc: null,
      form: { titulo: '', descripcion: '', casas: [] },
      errors: {},
      saving: false,
      dirty: false,
      confirmingDelete: false,
      addHouseId: '',
      housePhotos: [], // [{casa_id, titulo, fotos: [{id, url_min}]}]
      inherit: { titulo: '', descripcion: '' }, // lo que usa un flyer cuando deja vacío el título o la descripción
      formats: [],
      // vista previa
      previewFormat: '',
      previewKey: 0,
      previewScale: 1,
      // exportación
      exportFormats: {},
      exporting: false,
      exportResult: null,

      tipos: TIPOS,
      date: formatDate,
      tipoLabel(t) { return TIPOS[t] || t; },

      // El router cambia: #/documentos o #/documentos/{id}
      route(current, sub) {
        if (current !== 'documentos') return;
        queueMicrotask(() => this.applyRoute(sub));
      },

      async applyRoute(sub) {
        if (!sub) {
          this.view = 'list';
          await this.loadList();
        } else if (/^\d+$/.test(sub)) {
          await this.openEditor(Number(sub));
        } else {
          window.location.hash = '#/documentos';
        }
      },

      // ---- lista y creación ----
      async loadList() {
        try {
          const [docs, houses] = await Promise.all([window.api.get('/documentos'), window.api.get('/casas')]);
          this.docs = docs;
          this.houses = houses;
          this.listLoaded = true;
        } catch (e) { /* notificado por api.js */ }
      },

      startCreate(kind) {
        this.creating = this.creating === kind ? null : kind;
        this.newFlyerHouse = this.houses.length ? String(this.houses[0].id) : '';
        this.newCatTitle = '';
        this.newCatHouses = {};
        this.errors = {};
      },

      selectedNewHouses() {
        return this.houses.filter((h) => this.newCatHouses[h.id]).map((h) => h.id);
      },

      async createFlyer() {
        if (!this.newFlyerHouse) return;
        await this.createFlyerFor(Number(this.newFlyerHouse));
      },

      // también lo usa el botón "Crear flyer" del formulario de casa
      async createFlyerFor(houseId) {
        this.busy = true;
        try {
          const doc = await window.api.post('/documentos', { tipo: 'flyer', casa_id: houseId });
          window.notify('Flyer creado', 'ok');
          window.location.hash = '#/documentos/' + doc.id;
        } catch (e) { /* notificado */ } finally {
          this.busy = false;
        }
      },

      async createCatalog() {
        this.errors = {};
        const ids = this.selectedNewHouses();
        this.busy = true;
        try {
          const doc = await window.api.post('/documentos', {
            tipo: 'catalogo',
            titulo: this.newCatTitle,
            casas: ids.map((casa_id) => ({ casa_id })),
          }, { silent: true });
          window.notify('Catálogo creado', 'ok');
          window.location.hash = '#/documentos/' + doc.id;
        } catch (e) {
          if (e.fields && Object.keys(e.fields).length) this.errors = e.fields;
          else window.notify(e.message, 'error');
        } finally {
          this.busy = false;
        }
      },

      err(key) { return this.errors[key] || ''; },

      // ---- editor ----
      async openEditor(id) {
        this.view = 'editor';
        this.id = id;
        this.errors = {};
        this.confirmingDelete = false;
        this.exportResult = null;
        this.dirty = false;
        try {
          const [doc, formats, houses] = await Promise.all([
            window.api.get('/documentos/' + id),
            window.api.get('/formatos'),
            window.api.get('/casas'),
          ]);
          this.formats = formats;
          this.houses = houses;
          this.applyDoc(doc);
          if (!this.previewFormat || !formats.some((f) => f.id === this.previewFormat)) this.previewFormat = formats[0].id;
          const preferred = doc.formatos_preferidos || [];
          this.exportFormats = {};
          formats.forEach((f) => { this.exportFormats[f.id] = preferred.length ? preferred.includes(f.id) : true; });
          await this.loadHousePhotos();
          this.refreshPreview();
        } catch (e) {
          window.location.hash = '#/documentos';
        }
      },

      applyDoc(doc) {
        this.doc = doc;
        this.form = {
          titulo: doc.titulo == null ? '' : doc.titulo,
          descripcion: doc.descripcion == null ? '' : doc.descripcion,
          casas: doc.casas.map((c) => ({
            casa_id: c.casa_id,
            titulo: c.titulo,
            descripcion_corta: c.descripcion_corta == null ? '' : c.descripcion_corta,
            automatica: c.descripcion_corta_efectiva,
          })),
        };
        this.dirty = false;
      },

      isFlyer() { return this.doc && this.doc.tipo === 'flyer'; },
      isCatalog() { return this.doc && this.doc.tipo === 'catalogo'; },
      markDirty() { this.dirty = true; },

      payload() {
        const p = { titulo: this.form.titulo, descripcion: this.form.descripcion };
        if (this.isCatalog()) {
          p.casas = this.form.casas.map((c) => ({ casa_id: c.casa_id, descripcion_corta: c.descripcion_corta }));
        }
        return p;
      },

      async save() {
        this.errors = {};
        this.saving = true;
        try {
          const doc = await window.api.put('/documentos/' + this.id, this.payload(), { silent: true });
          this.applyDoc(doc);
          await this.loadHousePhotos();
          this.refreshPreview();
          window.notify('Documento guardado', 'ok');
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
          await window.api.del('/documentos/' + this.id);
          window.notify('Documento eliminado', 'ok');
          window.location.hash = '#/documentos';
        } catch (e) { /* notificado */ }
      },

      // ---- casas del catálogo ----
      availableHouses() {
        const used = new Set(this.form.casas.map((c) => c.casa_id));
        return this.houses.filter((h) => !used.has(h.id));
      },

      addHouse() {
        if (!this.addHouseId) return;
        const h = this.houses.find((x) => x.id === Number(this.addHouseId));
        if (!h) return;
        this.form.casas.push({ casa_id: h.id, titulo: h.titulo, descripcion_corta: '', automatica: '' });
        this.addHouseId = '';
        this.markDirty();
      },

      removeHouse(i) {
        this.form.casas.splice(i, 1);
        this.markDirty();
      },

      moveHouse(i, delta) {
        const j = i + delta;
        if (j < 0 || j >= this.form.casas.length) return;
        const [item] = this.form.casas.splice(i, 1);
        this.form.casas.splice(j, 0, item);
        this.markDirty();
      },

      // ---- portada ----
      async loadHousePhotos() {
        if (this.isFlyer()) {
          this.housePhotos = [];
          try {
            const h = await window.api.get('/casas/' + this.doc.casas[0].casa_id, { silent: true });
            this.inherit = { titulo: h.titulo, descripcion: h.descripcion };
          } catch (e) { /* la casa pudo borrarse */ }
          return;
        }
        const out = [];
        for (const c of this.doc.casas) {
          try {
            const h = await window.api.get('/casas/' + c.casa_id, { silent: true });
            if (h.fotos.length) out.push({ casa_id: c.casa_id, titulo: h.titulo, fotos: h.fotos });
          } catch (e) { /* la casa pudo borrarse */ }
        }
        this.housePhotos = out;
      },

      coverPhotoId() {
        return this.doc && this.doc.portada && this.doc.portada.origen === 'casa' ? this.doc.portada.foto_id : null;
      },

      async chooseCover(photoId) {
        try {
          const doc = await window.api.post('/documentos/' + this.id + '/portada', { foto_id: photoId });
          this.doc = doc;
          this.refreshPreview();
        } catch (e) { /* notificado */ }
      },

      async uploadCover(event) {
        const file = event.target.files[0];
        event.target.value = '';
        if (!file) return;
        const form = new FormData();
        form.append('archivo', file, file.name);
        this.busy = true;
        try {
          this.doc = await window.api.upload('/documentos/' + this.id + '/portada', form);
          window.notify('Portada actualizada', 'ok');
          this.refreshPreview();
        } catch (e) { /* notificado */ } finally {
          this.busy = false;
        }
      },

      // ---- vista previa ----
      format() { return this.formats.find((f) => f.id === this.previewFormat) || null; },

      previewUrl() {
        return this.id == null || !this.previewFormat ? 'about:blank' : '/render/' + this.id + '/' + this.previewFormat + '?v=' + this.previewKey;
      },

      refreshPreview() {
        this.previewKey = Date.now();
        this.$nextTick(() => this.fitPreview());
      },

      setPreviewFormat(id) {
        this.previewFormat = id;
        this.$nextTick(() => this.fitPreview());
      },

      // escala el iframe (que tiene el tamaño real del formato) para que quepa en el ancho disponible
      fitPreview() {
        const box = this.$refs.previewBox;
        const f = this.format();
        if (!box || !f) return;
        const maxHeight = Math.max(360, window.innerHeight - 220);
        this.previewScale = Math.min(1, box.clientWidth / f.ancho, maxHeight / f.alto);
      },

      previewFrameStyle() {
        const f = this.format();
        if (!f) return '';
        return 'width:' + f.ancho + 'px;height:' + f.alto + 'px;transform:scale(' + this.previewScale + ')';
      },

      previewBoxStyle() {
        const f = this.format();
        if (!f) return '';
        return 'width:' + Math.round(f.ancho * this.previewScale) + 'px;height:' + Math.round(f.alto * this.previewScale) + 'px';
      },

      // ---- exportación ----
      chosenFormats() { return this.formats.filter((f) => this.exportFormats[f.id]).map((f) => f.id); },

      async exportDocument() {
        const formatos = this.chosenFormats();
        if (!formatos.length) {
          window.notify('Elija al menos un formato', 'error');
          return;
        }
        if (this.dirty) {
          window.notify('Guarde los cambios antes de exportar', 'error');
          return;
        }
        this.exporting = true;
        this.exportResult = null;
        try {
          this.exportResult = await window.api.post('/documentos/' + this.id + '/exportar', { formatos });
        } catch (e) { /* notificado: p. ej. exportación en curso */ } finally {
          this.exporting = false;
        }
      },

      pdfMotivo(f) { return MOTIVOS_PDF[f.pdf_motivo] || f.pdf_mensaje || 'No se pudo generar el PDF.'; },
      hasPdfFailure() { return !!this.exportResult && this.exportResult.formatos.some((f) => f.pdf_estado !== 'generado'); },

      async openPath(path) {
        try {
          await window.api.post('/sistema/abrir-ruta', { ruta: path });
        } catch (e) { /* notificado */ }
      },
    }));
  });
})();
