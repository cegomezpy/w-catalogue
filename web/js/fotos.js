// Sección Fotos del formulario de casa: subida con cola, cuadrícula reordenable, portada, pie y eliminación.
// Se incorpora a `casasPage` como mixin (comparte `id`, `notify`, etc.), así que todo vive en el mismo ámbito de Alpine.
(function () {
  const MAX_FOTOS = 30;
  const MAX_BYTES = 25 * 1024 * 1024;
  const ADMITIDOS = ['image/jpeg', 'image/png'];

  // Subida con progreso por archivo (fetch no informa del avance de la subida).
  function uploadPhoto(houseId, file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/casas/' + houseId + '/fotos');
      xhr.upload.onprogress = (e) => { if (e.lengthComputable) onProgress(e.loaded / e.total); };
      xhr.onerror = () => reject(new Error('No se pudo comunicar con la aplicación'));
      xhr.onload = () => {
        let data = null;
        try { data = JSON.parse(xhr.responseText); } catch (e) { data = null; }
        if (xhr.status >= 200 && xhr.status < 300) resolve(data);
        else reject(new Error((data && data.error && data.error.message) || 'Error ' + xhr.status));
      };
      const form = new FormData();
      form.append('archivo', file, file.name);
      xhr.send(form);
    });
  }

  window.fotosMixin = function () {
    // fuera del estado reactivo: Alpine envuelve los objetos en proxies y FormData/Sortable no los admiten
    const files = new Map();
    let sortable = null;

    return {
      photos: [],
      queue: [], // {key, name, status: esperando|subiendo|error, progress, error}
      dragOver: false,
      photoToDelete: null,
      captionErrors: {},
      uploading: false,
      queueSeq: 0,

      maxFotos: MAX_FOTOS,

      setPhotos(list) {
        this.photos = list || [];
        this.queue = [];
        files.clear();
        this.photoToDelete = null;
        this.captionErrors = {};
      },

      // ---- subida ----
      pickFiles(event) {
        this.addFiles(event.target.files);
        event.target.value = ''; // permite volver a elegir los mismos archivos
      },

      dropFiles(event) {
        this.dragOver = false;
        if (this.id == null) return;
        this.addFiles(event.dataTransfer.files);
      },

      addFiles(fileList) {
        if (this.id == null) return;
        let planned = this.photos.length + this.queue.filter((q) => q.status !== 'error').length;
        Array.from(fileList).forEach((file) => {
          const entry = { key: ++this.queueSeq, name: file.name, status: 'esperando', progress: 0, error: '' };
          if (!ADMITIDOS.includes(file.type) && !/\.(jpe?g|png)$/i.test(file.name)) {
            entry.status = 'error';
            entry.error = 'Formato no admitido: solo se aceptan fotos JPEG y PNG.';
          } else if (file.size > MAX_BYTES) {
            entry.status = 'error';
            entry.error = 'La foto supera el máximo de 25 MB.';
          } else if (planned >= MAX_FOTOS) {
            entry.status = 'error';
            entry.error = 'La casa ya tiene el máximo de ' + MAX_FOTOS + ' fotos.';
          } else {
            planned += 1;
            files.set(entry.key, file);
          }
          this.queue.push(entry);
        });
        this.runQueue();
      },

      async runQueue() {
        if (this.uploading) return;
        this.uploading = true;
        const houseId = this.id;
        try {
          for (;;) {
            const entry = this.queue.find((q) => q.status === 'esperando');
            if (!entry || this.id !== houseId) break;
            entry.status = 'subiendo';
            try {
              const photo = await uploadPhoto(houseId, files.get(entry.key), (p) => { entry.progress = p; });
              if (this.id === houseId) this.photos.push(photo);
              files.delete(entry.key);
              this.queue = this.queue.filter((q) => q.key !== entry.key);
            } catch (e) {
              entry.status = 'error';
              entry.error = e.message;
            }
          }
        } finally {
          this.uploading = false;
        }
      },

      dismissQueued(key) {
        files.delete(key);
        this.queue = this.queue.filter((q) => q.key !== key);
      },

      // ---- cuadrícula reordenable ----
      initSortable(el) {
        if (!window.Sortable || sortable) return;
        sortable = window.Sortable.create(el, {
          animation: 150,
          draggable: '.foto-card',
          filter: 'input, button, .foto-confirm',
          preventOnFilter: false,
          ghostClass: 'foto-ghost',
          onEnd: (evt) => this.onSorted(evt),
        });
      },

      onSorted(evt) {
        const from = evt.oldDraggableIndex;
        const to = evt.newDraggableIndex;
        if (from === to || from == null || to == null) return;
        // Sortable ya movió el nodo; se deshace para que Alpine reordene la lista a partir de los datos
        const parent = evt.from;
        parent.removeChild(evt.item);
        const cards = Array.from(parent.querySelectorAll(':scope > .foto-card'));
        parent.insertBefore(evt.item, cards[from] || null);

        const next = this.photos.slice();
        const moved = next.splice(from, 1)[0];
        next.splice(to, 0, moved);
        this.photos = next;
        this.saveOrder();
      },

      makeCover(photo) {
        const i = this.photos.findIndex((p) => p.id === photo.id);
        if (i <= 0) return;
        const next = this.photos.slice();
        next.unshift(next.splice(i, 1)[0]);
        this.photos = next;
        this.saveOrder();
      },

      async saveOrder() {
        try {
          const list = await window.api.put('/casas/' + this.id + '/fotos/orden', { ids: this.photos.map((p) => p.id) });
          this.photos = list;
        } catch (e) {
          await this.reloadPhotos(); // el orden del servidor manda
        }
      },

      async reloadPhotos() {
        try {
          const h = await window.api.get('/casas/' + this.id);
          this.photos = h.fotos;
        } catch (e) { /* notificado */ }
      },

      // ---- pie de foto ----
      async saveCaption(photo, text) {
        const value = text.trim();
        if (value === photo.pie) return;
        delete this.captionErrors[photo.id];
        try {
          const updated = await window.api.put('/fotos/' + photo.id, { pie: value }, { silent: true });
          photo.pie = updated.pie;
        } catch (e) {
          this.captionErrors[photo.id] = (e.fields && e.fields.pie) || e.message;
        }
      },

      // ---- eliminar ----
      async deletePhoto() {
        const photo = this.photoToDelete;
        this.photoToDelete = null;
        if (!photo) return;
        try {
          await window.api.del('/fotos/' + photo.id);
          this.photos = this.photos.filter((p) => p.id !== photo.id);
          window.notify('Foto eliminada', 'ok');
        } catch (e) { /* notificado */ }
      },
    };
  };
})();
