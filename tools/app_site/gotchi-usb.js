// Browser port of tools/micropixel's UsbControlClient (MPX1 line protocol over
// USB Serial/JTAG), using Web Serial. Chrome / Edge only.

export class GotchiUsb {
  constructor() {
    this.port = null; this.reader = null; this.writer = null;
    this.buf = ''; this.nextId = (Math.random() * 0x7ffffffe | 0) + 1;
    this.chunkSize = 3072; this.queue = Promise.resolve();
  }

  static supported() { return 'serial' in navigator; }

  async connect(port) {
    this.port = port || await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
    await this.port.open({ baudRate: 115200 });
    try { await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch {}
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.pump();
    const deadline = Date.now() + 20000;
    let hello = null, lastError;
    while (!hello && Date.now() < deadline) {
      try { hello = await this.request('HELLO', '', 2000); } catch (e) { lastError = e; if (!/Timed out/.test(e.message)) throw e; }
    }
    if (!hello) throw lastError || new Error('The Gotchi did not answer');
    const f = hello.split(' ');
    if (f.length !== 4 || f[0] !== 'HELLO' || f[1] !== '1') throw new Error('Incompatible Gotchi firmware: ' + hello);
    this.chunkSize = Math.min(+f[2], this.chunkSize);
  }

  async disconnect() {
    try { await this.reader?.cancel(); } catch {}
    try { this.reader?.releaseLock(); this.writer?.releaseLock(); } catch {}
    try { await this.port?.close(); } catch {}
    this.port = null;
  }

  async pump() {
    const dec = new TextDecoder();
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.buf += dec.decode(value, { stream: true });
        if (this.buf.length > 131072) this.buf = this.buf.slice(-65536);
        this.wake?.();
      }
    } catch {}
  }

  allocId() { const id = this.nextId; this.nextId = id >= 0xffffffff ? 1 : id + 1; return id; }

  async send(id, op, args = '') {
    await this.writer.write(new TextEncoder().encode(`MPX1 ${id} ${op}${args ? ' ' + args : ''}\n`));
  }

  async read(id, timeout = 300000) {
    const prefix = `MPX1 ${id} `, deadline = Date.now() + timeout;
    for (;;) {
      let nl;
      while ((nl = this.buf.indexOf('\n')) >= 0) {
        const line = this.buf.slice(0, nl).replace(/\r$/, ''); this.buf = this.buf.slice(nl + 1);
        if (!line.startsWith(prefix)) continue;
        const rest = line.slice(prefix.length), sp = rest.indexOf(' ');
        if (sp < 0) throw new Error('Malformed reply from the Gotchi');
        const status = rest.slice(0, sp), detail = rest.slice(sp + 1);
        if (status === 'ERROR') throw new Error('Gotchi refused: ' + detail);
        if (status !== 'OK') throw new Error('Unknown reply status: ' + status);
        return detail;
      }
      const left = deadline - Date.now();
      if (left <= 0) throw new Error('Timed out waiting for the Gotchi');
      await new Promise(r => { this.wake = r; setTimeout(r, Math.min(left, 200)); });
    }
  }

  // One command at a time, like the CLI.
  request(op, args = '', timeout) {
    const run = async () => { const id = this.allocId(); await this.send(id, op, args); return this.read(id, timeout); };
    const p = this.queue.then(run, run); this.queue = p.catch(() => {}); return p;
  }

  async status() {
    const f = (await this.request('DEVICE_STATUS')).split(' ');
    if (f.length !== 17 || f[0] !== 'DEVICE_STATUS') throw new Error('Malformed status from the Gotchi');
    const [version, board, chip] = f.slice(1, 4).map(b64text);
    const chipU = chip.trim().toUpperCase();
    return {
      firmware: version, board, chip,
      target: chipU === 'ESP32-S3' ? 'xtensa' : 'riscv32-ilp32f',
      activeAppId: f[5] === '-' ? null : f[5],
      width: +f[12], height: +f[13],
      storeUsed: +f[8], storeTotal: +f[9],
    };
  }

  async listApps() {
    const apps = []; let offset = 0, count = null, used = 0, total = 0;
    while (count === null || offset < count) {
      const f = (await this.request('APP_LIST', String(offset))).split(' ').filter(Boolean);
      if (f.length < 6 || f[0] !== 'APP_LIST') throw new Error('Malformed app list from the Gotchi');
      const [next, c, u, t, pageCount] = f.slice(1, 6).map(Number);
      count = c; used = u; total = t;
      for (const entry of f.slice(6, 6 + pageCount)) {
        const p = entry.split(',');
        apps.push({ appId: p[0], sizeBytes: +p[1], displayName: b64text(p[2]), active: p[3] === '1', lifecycle: p[4], sha256: p[5] || null, version: p[6] || null });
      }
      offset = next; if (offset === count) break;
    }
    return { apps, storeUsed: used, storeTotal: total };
  }

  async action(op, appId = '') {
    const d = await this.request(op, appId);
    if (!d.startsWith('RESULT ')) throw new Error('Unexpected reply: ' + d);
    return d.slice(7);
  }
  start(appId) { return this.action('APP_START', appId); }
  stop(appId) { return this.action('APP_STOP', appId || ''); }
  uninstall(appId) { return this.action('APP_UNINSTALL', appId); }

  // bytes: Uint8Array of a Bundle v1. onProgress(0..100)
  async install(appId, bytes, onProgress = () => {}) {
    const digest = hex(await crypto.subtle.digest('SHA-256', bytes));
    const run = async () => {
      const id = this.allocId();
      await this.send(id, 'APP_INSTALL_BEGIN', `${appId} ${bytes.length} ${digest}`);
      const ready = (await this.read(id)).split(' ');
      if (ready.length !== 2 || ready[0] !== 'INSTALL_READY') throw new Error('Gotchi rejected the install: ' + ready.join(' '));
      const size = Math.min(this.chunkSize, +ready[1]);
      onProgress(0);
      try {
        for (let off = 0; off < bytes.length; off += size) {
          const chunk = bytes.subarray(off, off + size);
          await this.send(id, 'APP_INSTALL_CHUNK', `${off} ${b64(chunk)}`);
          const ack = await this.read(id), want = off + chunk.length;
          if (ack !== `INSTALL_CHUNK ${want}`) throw new Error('Unexpected install offset: ' + ack);
          onProgress(Math.min(99, Math.floor(want * 99 / bytes.length)));
        }
        await this.send(id, 'APP_INSTALL_COMMIT');
        const done = await this.read(id);
        if (!done.startsWith('RESULT ')) throw new Error('Unexpected install result: ' + done);
        onProgress(100);
        return done.slice(7);
      } catch (e) {
        try { await this.send(id, 'APP_INSTALL_ABORT'); } catch {}
        throw e;
      }
    };
    const p = this.queue.then(run, run); this.queue = p.catch(() => {}); return p;
  }
}

function b64text(s) { return new TextDecoder().decode(Uint8Array.from(atob(s), c => c.charCodeAt(0))); }
function b64(u8) { let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(s); }
function hex(buf) { return [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join(''); }
