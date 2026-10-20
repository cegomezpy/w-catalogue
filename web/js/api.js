// Cliente de la API local. Interpreta el formato común de error
// {"error": {"code": "...", "message": "..."}} y lo muestra como notificación.
(function () {
  class ApiError extends Error {
    constructor(status, code, message, fields) {
      super(message);
      this.status = status;
      this.code = code;
      this.fields = fields || {};
    }
  }

  async function request(method, path, body, opts) {
    const options = { method, headers: {} };
    if (body instanceof FormData) {
      options.body = body; // el navegador fija el multipart y su boundary
    } else if (body !== undefined) {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(body);
    }

    let res;
    try {
      res = await fetch('/api' + path, options);
    } catch (e) {
      const err = new ApiError(0, 'network', 'No se pudo comunicar con la aplicación');
      if (!(opts && opts.silent)) window.notify(err.message, 'error');
      throw err;
    }

    let data = null;
    const text = await res.text();
    if (text) {
      try { data = JSON.parse(text); } catch (e) { data = null; }
    }

    if (!res.ok) {
      const e = data && data.error ? data.error : {};
      const err = new ApiError(res.status, e.code || 'error', e.message || 'Error ' + res.status, e.fields);
      if (!(opts && opts.silent)) window.notify(err.message, 'error');
      throw err;
    }
    return data;
  }

  window.ApiError = ApiError;
  window.api = {
    get: (path, opts) => request('GET', path, undefined, opts),
    post: (path, body, opts) => request('POST', path, body === undefined ? {} : body, opts),
    upload: (path, formData, opts) => request('POST', path, formData, opts),
    put: (path, body, opts) => request('PUT', path, body === undefined ? {} : body, opts),
    del: (path, opts) => request('DELETE', path, undefined, opts),
  };
})();
