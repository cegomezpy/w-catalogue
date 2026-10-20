// Estado global de la interfaz: notificaciones, canal de presencia y página de inicio.
(function () {
  document.addEventListener('alpine:init', () => {
    Alpine.store('toasts', {
      items: [],
      next: 1,
      push(message, kind) {
        const id = this.next++;
        this.items.push({ id, message, kind: kind || 'info' });
        setTimeout(() => this.dismiss(id), 6000);
      },
      dismiss(id) {
        this.items = this.items.filter((t) => t.id !== id);
      },
    });

    Alpine.store('session', { connected: false, everConnected: false });

    // El servidor cierra la aplicación cuando no queda ninguna conexión de presencia abierta.
    // EventSource se reconecta solo, lo que tolera las recargas de la página.
    const events = new EventSource('/api/session/events');
    events.onopen = () => {
      const s = Alpine.store('session');
      s.connected = true;
      s.everConnected = true;
    };
    events.onerror = () => { Alpine.store('session').connected = false; };

    Alpine.data('homePage', () => ({
      info: null,
      failed: false,
      async load() {
        try {
          this.info = await window.api.get('/info');
        } catch (e) {
          this.failed = true;
        }
      },
    }));
  });

  window.notify = (message, kind) => {
    if (window.Alpine) Alpine.store('toasts').push(message, kind);
    else console.error(message);
  };
})();
