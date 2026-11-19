#!/usr/bin/env node
// Runtime test for the browser reconnect path. It drives a real headless
// Chrome through the DevTools protocol, starts real harness listeners on
// loopback, and verifies the states a customer sees when the console process
// stops, restarts, moves to another origin, or returns with a stale token.
//
// This intentionally has no npm dependency. Chrome and its DevTools endpoint
// are the only browser prerequisites; make skips the target when either Chrome
// or node is unavailable.

import assert from 'node:assert/strict';
import { execFileSync, spawn } from 'node:child_process';
import {
  closeSync, existsSync, mkdirSync, mkdtempSync, openSync, readFileSync,
  rmSync, writeFileSync
} from 'node:fs';
import http from 'node:http';
import https from 'node:https';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const ROOT = process.cwd();
const HARNESS = join(ROOT, 'build', 'harness');
const TOKEN_A = 'browser-reconnect-token-a-0123456789';
const TOKEN_B = 'browser-reconnect-token-b-0123456789';
const HOST_A = '127.0.0.1';
let HOST_B = '127.0.0.2';
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function findBrowser() {
  const candidates = [
    process.env.CHROME_BIN,
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/Applications/Chromium.app/Contents/MacOS/Chromium',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser'
  ];
  for (const candidate of candidates) {
    if (candidate && existsSync(candidate)) return candidate;
  }
  for (const name of ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']) {
    try {
      const found = execFileSync('which', [name], { encoding: 'utf8' }).trim();
      if (found) return found;
    } catch (_) {
      // Try the next executable name.
    }
  }
  return '';
}

const CHROME = findBrowser();

function tail(path) {
  try {
    const text = readFileSync(path, 'utf8');
    return text.length > 4000 ? text.slice(-4000) : text;
  } catch (_) {
    return '';
  }
}

async function freePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, HOST_A, resolve);
  });
  const port = server.address().port;
  await new Promise((resolve) => server.close(resolve));
  return port;
}

const allocatedPorts = new Set();

async function testPort() {
  for (;;) {
    const port = await freePort();
    if (allocatedPorts.has(port)) continue;
    allocatedPorts.add(port);
    return port;
  }
}

function requestStatus(url, timeout = 1200) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url);
    const client = parsed.protocol === 'https:' ? https : http;
    const request = client.get(parsed, { rejectUnauthorized: false }, (response) => {
      response.resume();
      if (response.statusCode === 200) resolve();
      else reject(new Error('HTTP ' + response.statusCode));
    });
    request.setTimeout(timeout, () => request.destroy(new Error('request timed out')));
    request.on('error', reject);
  });
}

async function waitForProbe(url, timeout = 20000) {
  const deadline = Date.now() + timeout;
  let last = null;
  while (Date.now() < deadline) {
    try {
      await requestStatus(url);
      return;
    } catch (error) {
      last = error;
      await sleep(100);
    }
  }
  throw new Error('endpoint did not become ready: ' + url + ' (' + (last && last.message) + ')');
}

function writeSession(path, prompt, markers) {
  const messages = [{ role: 'system', content: prompt }];
  for (const marker of markers) {
    messages.push({ role: 'user', content: 'user transcript ' + marker });
    messages.push({ role: 'assistant', content: 'assistant transcript ' + marker });
  }
  writeFileSync(path, JSON.stringify({
    version: 1,
    system_prompt: prompt,
    context_chars: 120000,
    compactions: 0,
    messages
  }, null, 2));
}

async function stopChild(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  await new Promise((resolve) => {
    const timer = setTimeout(() => {
      try { child.kill('SIGKILL'); } catch (_) { /* already gone */ }
    }, 3000);
    child.once('exit', () => {
      clearTimeout(timer);
      resolve();
    });
    try {
      child.kill('SIGTERM');
    } catch (_) {
      clearTimeout(timer);
      resolve();
    }
  });
  await sleep(120);
}

function getJson(url, token, timeout = 1200) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url);
    const client = parsed.protocol === 'https:' ? https : http;
    const request = client.get(parsed, {
      rejectUnauthorized: false,
      headers: { Authorization: 'Bearer ' + token }
    }, (response) => {
      let body = '';
      response.setEncoding('utf8');
      response.on('data', (chunk) => { body += chunk; });
      response.on('end', () => {
        if (response.statusCode !== 200) {
          reject(new Error('HTTP ' + response.statusCode));
          return;
        }
        try { resolve(JSON.parse(body)); }
        catch (error) { reject(error); }
      });
    });
    request.setTimeout(timeout, () => request.destroy(new Error('request timed out')));
    request.on('error', reject);
  });
}

function startHarness({ label, config, cert, key, httpPort, httpsPort,
                        token, session, log }) {
  const args = [
    '--config', config,
    '--serve', String(httpPort),
    '--serve-tls', String(httpsPort),
    '--tls-cert', cert,
    '--tls-key', key,
    '--discovery-port', '0',
    '--serve-token', token,
    '--session', session,
    '--log', log
  ];
  const fd = openSync(log, 'a');
  const child = spawn(HARNESS, args, {
    cwd: ROOT,
    stdio: ['ignore', fd, fd]
  });
  closeSync(fd);
  const instance = {
    label,
    child,
    async ready() {
      try {
        await waitForProbe('http://' + HOST_A + ':' + httpPort + '/.well-known/ps5h');
        await waitForProbe('https://' + HOST_A + ':' + httpsPort + '/.well-known/ps5h');
      } catch (error) {
        throw new Error(label + ' did not start: ' + error.message + '\n' +
                        tail(log));
      }
    },
    async health() {
      return getJson('https://' + HOST_A + ':' + httpsPort + '/api/health', token);
    },
    async stop() { await stopChild(child); }
  };
  return instance;
}

class CDP {
  constructor(url) {
    this.url = url;
    this.ws = null;
    this.nextId = 0;
    this.pending = new Map();
    this.listeners = new Map();
  }

  async open() {
    await new Promise((resolve, reject) => {
      const ws = new WebSocket(this.url);
      this.ws = ws;
      const opened = () => {
        ws.removeEventListener('open', opened);
        ws.removeEventListener('error', failed);
        resolve();
      };
      const failed = (event) => {
        ws.removeEventListener('open', opened);
        ws.removeEventListener('error', failed);
        reject(new Error('DevTools WebSocket failed: ' + (event && event.message || 'error')));
      };
      ws.addEventListener('open', opened);
      ws.addEventListener('error', failed);
      ws.addEventListener('message', (event) => this.handle(event));
      ws.addEventListener('close', () => {
        for (const item of this.pending.values()) {
          item.reject(new Error('DevTools WebSocket closed'));
        }
        this.pending.clear();
      });
    });
  }

  handle(event) {
    let message;
    try { message = JSON.parse(String(event.data)); }
    catch (_) { return; }
    if (message.id) {
      const item = this.pending.get(message.id);
      if (!item) return;
      this.pending.delete(message.id);
      if (message.error) item.reject(new Error(message.error.message || 'CDP error'));
      else item.resolve(message.result || {});
      return;
    }
    const handlers = this.listeners.get(message.method) || [];
    for (const handler of handlers) handler(message);
  }

  send(method, params = {}, sessionId = undefined) {
    const id = ++this.nextId;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      const message = { id, method, params };
      if (sessionId) message.sessionId = sessionId;
      try {
        this.ws.send(JSON.stringify(message));
      } catch (error) {
        this.pending.delete(id);
        reject(error);
      }
    });
  }

  on(method, handler) {
    const handlers = this.listeners.get(method) || [];
    handlers.push(handler);
    this.listeners.set(method, handlers);
  }

  close() {
    try { this.ws && this.ws.close(); } catch (_) { /* already closed */ }
  }
}

async function waitFor(label, predicate, timeout = 30000) {
  const deadline = Date.now() + timeout;
  let last = null;
  while (Date.now() < deadline) {
    try {
      const value = await predicate();
      if (value) return value;
    } catch (error) {
      last = error;
    }
    await sleep(100);
  }
  throw new Error('timed out waiting for ' + label + (last ? ': ' + last.message : ''));
}

function sanitize(snapshot) {
  return {
    ...snapshot,
    token: snapshot.token ? 'present(' + snapshot.token.length + ')' : 'missing'
  };
}

async function main() {
  if (typeof WebSocket !== 'function' || typeof fetch !== 'function') {
    console.log('reconnect_browser_test: SKIP (node WebSocket/fetch unavailable)');
    return;
  }
  if (!CHROME) {
    console.log('reconnect_browser_test: SKIP (Chrome/Chromium not found)');
    return;
  }
  if (!existsSync(HARNESS)) {
    throw new Error('build/harness is missing; run make build/harness first');
  }
  try {
    execFileSync('openssl', ['version'], { stdio: 'ignore' });
  } catch (_) {
    console.log('reconnect_browser_test: SKIP (openssl not found)');
    return;
  }

  const temp = mkdtempSync(join(tmpdir(), 'reconnect-browser-'));
  const config = join(temp, 'config.json');
  const cert = join(temp, 'cert.pem');
  const key = join(temp, 'key.pem');
  const httpA = await testPort();
  const httpsA = await testPort();
  const httpB = await testPort();
  const httpsB = await testPort();
  assert.equal(new Set([httpA, httpsA, httpB, httpsB]).size, 4, 'test ports must be distinct');
  const sessionA = join(temp, 'session-a.json');
  const sessionB = join(temp, 'session-b.json');
  const logA = join(temp, 'harness-a.log');
  const logB = join(temp, 'harness-b.log');
  const evidenceDir = join(ROOT, 'build', 'reconnect-evidence');
  rmSync(evidenceDir, { recursive: true, force: true });
  mkdirSync(evidenceDir, { recursive: true });

  writeFileSync(config, JSON.stringify({
    model: { base_url: 'http://127.0.0.1:9/v1', model: 'browser-test',
             api_key_env: 'PS5H_BROWSER_TEST_KEY' },
    endpoints_dir: '',
    plugins_dir: '',
    skills_dir: '',
    allowed_roots: [temp],
    read_roots: [temp],
    system_prompt: 'browser reconnect test'
  }, null, 2));
  try {
    execFileSync('openssl', [
      'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256',
      '-days', '1', '-keyout', key, '-out', cert,
      '-subj', '/CN=127.0.0.1',
      '-addext', 'subjectAltName=IP:127.0.0.1,IP:127.0.0.2,DNS:localhost'
    ], { stdio: 'ignore' });
  } catch (_) {
    execFileSync('openssl', [
      'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256',
      '-days', '1', '-keyout', key, '-out', cert, '-subj', '/CN=127.0.0.1'
    ], { stdio: 'ignore' });
  }

  writeSession(sessionA, 'browser reconnect test', ['initial-marker']);
  writeSession(sessionB, 'browser reconnect test', ['moved-marker']);

  let serverA = null;
  let serverB = null;
  let browser = null;
  let cdp = null;
  let sessionId = null;
  let chromeProfile = '';
  const pageErrors = [];
  const report = {
    browser: CHROME,
    origins: {},
    ports: { httpA, httpsA, httpB, httpsB },
    steps: [],
    success: false
  };

  try {
    serverA = startHarness({ label: 'harness A', config, cert, key,
      httpPort: httpA, httpsPort: httpsA, token: TOKEN_A, session: sessionA,
      log: logA });
    await serverA.ready();

    // Same-process resume: SIGCONT must make the server close and rebind both
    // listeners, including the TLS one. The page test below then covers the
    // browser recovering from the brief outage on the same origin.
    const generationBeforeResume = (await serverA.health()).listener_generation;
    assert.equal(process.kill(serverA.child.pid, 'SIGCONT'), true);
    const resumed = await waitFor('TLS listener after SIGCONT', async () => {
      try {
        const health = await serverA.health();
        return health.listener_generation > generationBeforeResume ? health : false;
      } catch (_) {
        return false;
      }
    }, 15000);
    assert.ok(resumed.listener_generation > generationBeforeResume);
    report.steps.push({ name: 'tls-listener-resumed',
                        generation: resumed.listener_generation });
    console.log('reconnect_browser_test: tls-listener-resumed (generation ' +
                resumed.listener_generation + ')');

    chromeProfile = mkdtempSync(join(tmpdir(), 'reconnect-chrome-'));
    let chromeStderr = '';
    browser = spawn(CHROME, [
      '--headless=new',
      '--disable-gpu',
      '--no-first-run',
      '--no-default-browser-check',
      '--disable-background-networking',
      '--disable-component-update',
      '--remote-debugging-port=0',
      '--remote-allow-origins=*',
      '--user-data-dir=' + chromeProfile,
      '--ignore-certificate-errors',
      'about:blank'
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    browser.stderr.on('data', (chunk) => {
      chromeStderr += String(chunk);
      if (chromeStderr.length > 8000) chromeStderr = chromeStderr.slice(-8000);
    });
    const activePortFile = join(chromeProfile, 'DevToolsActivePort');
    await waitFor('Chrome DevTools endpoint', () => existsSync(activePortFile), 20000);
    const active = readFileSync(activePortFile, 'utf8').trim().split('\n');
    const debugPort = Number(active[0]);
    const browserUrl = 'ws://127.0.0.1:' + debugPort + active[1];
    cdp = new CDP(browserUrl);
    await cdp.open();
    const target = await cdp.send('Target.createTarget', { url: 'about:blank' });
    const attached = await cdp.send('Target.attachToTarget', {
      targetId: target.targetId, flatten: true
    });
    sessionId = attached.sessionId;
    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);
    await cdp.send('Emulation.setDeviceMetricsOverride', {
      width: 1280, height: 900, deviceScaleFactor: 1, mobile: false
    }, sessionId);
    cdp.on('Runtime.exceptionThrown', (event) => {
      const details = event.params && event.params.exceptionDetails || {};
      pageErrors.push(details.text || 'page exception');
    });

    const evalPage = async (expression) => {
      const result = await cdp.send('Runtime.evaluate', {
        expression, returnByValue: true, awaitPromise: true, userGesture: true
      }, sessionId);
      if (result.exceptionDetails) {
        const details = result.exceptionDetails;
        throw new Error('page evaluation failed: ' +
                        (details.exception && details.exception.description || details.text));
      }
      return result.result ? result.result.value : undefined;
    };
    const snapshotExpression = `(() => {
      const visible = (id) => {
        const el = document.getElementById(id);
        if (!el) return false;
        const style = getComputedStyle(el);
        return !el.classList.contains('hidden') && style.display !== 'none' &&
          style.visibility !== 'hidden';
      };
      const text = (id) => {
        const el = document.getElementById(id);
        return el ? el.innerText : '';
      };
      const nav = performance.getEntriesByType('navigation')[0] || {};
      return {
        ready: document.readyState,
        href: location.href,
        origin: location.origin,
        search: location.search,
        navType: nav.type || '',
        conn: text('conn'),
        reconnectVisible: visible('reconnect'),
        reconnectDetail: text('reconnect-detail'),
        loginVisible: visible('login'),
        loginError: text('login-error'),
        messages: Array.from(document.querySelectorAll('#messages .msg .text'))
          .map((el) => el.innerText).join(String.fromCharCode(10)),
        token: sessionStorage.getItem('ps5h-token') || ''
      };
    })()`;
    const snapshot = () => evalPage(snapshotExpression);
    const waitSnapshot = (label, predicate, timeout = 30000) => waitFor(
      label, async () => {
        const state = await snapshot();
        return predicate(state) ? state : false;
      }, timeout);
    const screenshot = async (name) => {
      const result = await cdp.send('Page.captureScreenshot', {
        format: 'png', fromSurface: true
      }, sessionId);
      writeFileSync(join(evidenceDir, name + '.png'),
                    Buffer.from(result.data, 'base64'));
    };
    const record = async (name, state) => {
      await screenshot(name);
      report.steps.push({ name, snapshot: sanitize(state) });
      console.log('reconnect_browser_test: ' + name + ' (' + state.origin + ')');
    };

    const originA = 'https://' + HOST_A + ':' + httpsA;
    report.origins.a = originA;
    await cdp.send('Page.navigate', {
      url: originA + '/?token=' + encodeURIComponent(TOKEN_A)
    }, sessionId);
    let state = await waitSnapshot('initial connected page', (value) =>
      value.ready === 'complete' && value.conn.indexOf('Connected to console') >= 0 &&
      !value.reconnectVisible && value.messages.indexOf('initial-marker') >= 0);
    assert.equal(state.origin, originA);
    assert.equal(state.search, '');
    assert.equal(state.token, TOKEN_A);
    assert.equal(state.loginVisible, false);
    assert.equal(state.navType, 'navigate');
    await record('01-connected-https', state);

    await serverA.stop();
    state = await waitSnapshot('lost-connection banner', (value) =>
      value.reconnectVisible && value.conn.indexOf('Disconnected') >= 0);
    assert.match(state.reconnectDetail, /Retrying this address automatically/);
    assert.equal(state.loginVisible, false);
    await record('02-lost-banner', state);

    // Change the persisted transcript while the process is down. The page's
    // automatic recovery must fetch this new history, not just reuse the DOM.
    writeSession(sessionA, 'browser reconnect test', ['initial-marker', 'recovered-marker']);
    serverA = startHarness({ label: 'harness A restart', config, cert, key,
      httpPort: httpA, httpsPort: httpsA, token: TOKEN_A, session: sessionA,
      log: logA });
    await serverA.ready();
    state = await waitSnapshot('same-origin automatic recovery', (value) =>
      !value.reconnectVisible && value.loginVisible === false &&
      value.conn.indexOf('Connected to console') >= 0 &&
      value.messages.indexOf('recovered-marker') >= 0);
    assert.equal(state.origin, originA);
    assert.equal(state.search, '');
    assert.equal(state.token, TOKEN_A);
    // A page reload would replace the navigation entry and change its type.
    assert.equal(state.navType, 'navigate');
    assert.match(state.messages, /initial-marker/);
    assert.match(state.messages, /recovered-marker/);
    await record('03-same-origin-recovered', state);

    serverB = startHarness({ label: 'harness B', config, cert, key,
      httpPort: httpB, httpsPort: httpsB, token: TOKEN_A, session: sessionB,
      log: logB });
    await serverB.ready();
    // Switch to the other listener as well as a different port. The initial
    // page exercises HTTPS; B exercises the HTTP listener.
    let originB = 'http://' + HOST_B + ':' + httpB;
    try {
      await waitForProbe('http://' + HOST_B + ':' + httpB + '/.well-known/ps5h', 4000);
    } catch (_) {
      HOST_B = HOST_A;
      originB = 'http://' + HOST_B + ':' + httpB;
      console.log('reconnect_browser_test: loopback alias unavailable; using a changed port only');
    }
    report.origins.b = originB;

    await serverA.stop();
    state = await waitSnapshot('lost before address switch', (value) =>
      value.reconnectVisible && value.conn.indexOf('Disconnected') >= 0);
    await record('04-lost-before-switch', state);

    await evalPage(`(() => {
      const input = document.getElementById('reconnect-url');
      input.value = ${JSON.stringify(originB)};
      input.dispatchEvent(new Event('input', { bubbles: true }));
      return input.value;
    })()`);
    await evalPage(`document.getElementById('reconnect-use').click(); true`);
    state = await waitSnapshot('changed address and port', (value) =>
      value.origin === originB && value.conn.indexOf('Connected to console') >= 0 &&
      !value.reconnectVisible && !value.loginVisible &&
      value.messages.indexOf('moved-marker') >= 0);
    assert.equal(state.search, '');
    assert.equal(state.token, TOKEN_A);
    assert.match(state.messages, /moved-marker/);
    assert.doesNotMatch(state.messages, /recovered-marker/);
    await record('05-changed-address', state);

    await serverB.stop();
    state = await waitSnapshot('lost before stale-token restart', (value) =>
      value.reconnectVisible && value.conn.indexOf('Disconnected') >= 0);
    await record('06-lost-before-stale-token', state);

    serverB = startHarness({ label: 'harness B with new token', config, cert, key,
      httpPort: httpB, httpsPort: httpsB, token: TOKEN_B, session: sessionB,
      log: logB });
    await serverB.ready();
    state = await waitSnapshot('stale token rejected', (value) =>
      value.loginVisible && value.loginError.indexOf('Token rejected') >= 0);
    assert.equal(state.reconnectVisible, false);
    assert.match(state.loginError, /generated token/);
    assert.match(state.conn, /Token rejected/);
    assert.equal(state.token, TOKEN_A);
    await record('07-stale-token-login', state);

    report.success = true;
    report.pageErrors = pageErrors;
    report.chromeStderr = chromeStderr.slice(-2000);
    writeFileSync(join(evidenceDir, 'report.json'), JSON.stringify(report, null, 2));
    console.log('reconnect_browser_test: ok');
    console.log('reconnect_browser_test: evidence ' + evidenceDir);
    console.log('reconnect_browser_test: browser=' + CHROME + ' ports=' +
                [httpA, httpsA, httpB, httpsB].join(','));
  } catch (error) {
    report.error = error && error.stack || String(error);
    report.pageErrors = pageErrors;
    try {
      if (cdp && sessionId) {
        const state = await (async () => {
          const result = await cdp.send('Runtime.evaluate', {
            expression: '({href:location.href, title:document.title})',
            returnByValue: true
          }, sessionId);
          return result.result && result.result.value;
        })();
        report.failurePage = state;
        const result = await cdp.send('Page.captureScreenshot', {
          format: 'png', fromSurface: true
        }, sessionId);
        writeFileSync(join(evidenceDir, 'failure.png'),
                      Buffer.from(result.data, 'base64'));
      }
    } catch (_) { /* best-effort evidence */ }
    writeFileSync(join(evidenceDir, 'report.json'), JSON.stringify(report, null, 2));
    throw error;
  } finally {
    if (cdp) cdp.close();
    if (browser) await stopChild(browser);
    if (chromeProfile) rmSync(chromeProfile, { recursive: true, force: true });
    if (serverB) await serverB.stop();
    if (serverA) await serverA.stop();
    if (!process.env.PS5H_KEEP_RECONNECT_TEMP) {
      rmSync(temp, { recursive: true, force: true });
    } else {
      console.log('reconnect_browser_test: kept temp ' + temp);
    }
  }
}

main().catch((error) => {
  console.error('reconnect_browser_test: FAILED');
  console.error(error && error.stack || String(error));
  process.exitCode = 1;
});