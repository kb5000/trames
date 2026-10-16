// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const net = require('node:net');
const { fork } = require('node:child_process');
if (process.argv[2] === 'child') {
  process.on('message', (message, socket) => {
    assert.equal(message, 'socket');
    socket.end('guest socket handoff\n', () => process.disconnect());
  });
  process.send('ready');
} else {
  const timer = setTimeout(() => { console.error('socket handoff timeout'); process.exit(1); }, 300000);
  let exited = false, received = false;
  function finish() {
    if (exited && received) { clearTimeout(timer); console.log('node-socket-handoff-ok'); }
  }
  const child = fork(__filename, ['child'], { stdio: ['ignore', 'inherit', 'inherit', 'ipc'] });
  child.once('error', error => { throw error; });
  child.once('message', message => {
    assert.equal(message, 'ready');
    const server = net.createServer(socket => child.send('socket', socket));
    server.listen(0, '127.0.0.1', () => {
      const client = net.connect(server.address().port, '127.0.0.1');
      const chunks = [];
      client.on('data', bytes => chunks.push(bytes));
      client.on('error', error => { throw error; });
      client.on('end', () => {
        assert.equal(Buffer.concat(chunks).toString(), 'guest socket handoff\n');
        server.close();
        received = true;
        finish();
      });
    });
  });
  child.once('exit', code => {
    assert.equal(code, 0);
    exited = true;
    finish();
  });
}
