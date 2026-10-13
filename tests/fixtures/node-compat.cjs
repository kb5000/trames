// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const crypto = require('node:crypto');
const zlib = require('node:zlib');
const { promisify } = require('node:util');
const { Worker } = require('node:worker_threads');
const { execFile } = require('node:child_process');
const http = require('node:http');
const dns = require('node:dns/promises');

async function main() {
  process.title = 'trames';
  assert.equal(process.title, 'trames');
  // Repeated arithmetic exercises ordinary V8 tiering without --jitless.
  function sum(n) { let s = 0; for (let i = 0; i < n; ++i) s += i; return s; }
  for (let i = 0; i < 100; ++i) assert.equal(sum(10000), 49995000);
  assert.equal(new Intl.NumberFormat('en-US').format(12345), '12,345');
  assert.equal(new URL('https://example.org/a?x=1').searchParams.get('x'), '1');
  const text = 'retained Node.js data\n';
  await fs.writeFile('/root/node-retained.txt', text);
  assert.equal(await fs.readFile('/root/node-retained.txt', 'utf8'), text);
  assert.equal(JSON.parse(JSON.stringify({ answer: 42 })).answer, 42);
  assert.equal(crypto.createHash('sha256').update('abc').digest('hex'),
    'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  assert.equal(zlib.gunzipSync(zlib.gzipSync(text)).toString(), text);
  assert.equal((await promisify(crypto.pbkdf2)('password', 'salt', 2, 16, 'sha256')).length, 16);
  await new Promise(resolve => setTimeout(resolve, 5));
  console.log('node fs/JSON/Intl/crypto/zlib/timers: ok');
  const worker = new Worker(`require('node:worker_threads').parentPort.postMessage(6 * 7)`, { eval: true });
  const value = await new Promise((resolve, reject) => { worker.once('message', resolve); worker.once('error', reject); });
  assert.equal(value, 42);
  await worker.terminate();
  const child = await promisify(execFile)('/bin/sh', ['-c', 'printf child-ok']);
  assert.equal(child.stdout, 'child-ok');
  assert.equal(child.stderr, '');
  console.log('node workers/child_process: ok');
  console.log('node localhost lookup: starting');
  assert.equal((await dns.lookup('localhost', { family: 4 })).address, '127.0.0.1');
  console.log('node HTTP server/fetch: starting');
  const local = http.createServer((request, response) => response.end('loopback-ok'));
  await new Promise(resolve => local.listen(0, '127.0.0.1', resolve));
  assert.equal(await (await fetch(`http://127.0.0.1:${local.address().port}/`, { signal: AbortSignal.timeout(120000) })).text(), 'loopback-ok');
  console.log('node HTTP server/fetch: received response');
  await new Promise((resolve, reject) => local.close(error => error ? reject(error) : resolve()));
  console.log('node localhost lookup/HTTP server/fetch: ok');
  const bytes = await new Promise((resolve, reject) => {
    http.get(process.argv[2], response => {
      assert.equal(response.statusCode, 200);
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve(Buffer.concat(chunks)));
      response.on('error', reject);
    }).on('error', reject);
  });
  assert.equal(bytes[0], 0x1f); assert.equal(bytes[1], 0x8b);
  console.log('node HTTP virtual egress: ok');
  console.log('node compatibility: ok');
}
main().catch(error => { console.error(error); process.exit(1); });
