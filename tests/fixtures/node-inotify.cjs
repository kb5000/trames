// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs = require('fs');
const path = '/tmp/trames-node-watch-' + process.pid;
fs.mkdirSync(path);
let renamed = false;
let changed = false;
const timer = setTimeout(() => { console.error('missing fs.watch events', {renamed, changed}); process.exit(1); }, 15000);
const watcher = fs.watch(path, (event, filename) => {
  if (filename !== 'hello') return;
  renamed ||= event === 'rename';
  changed ||= event === 'change';
  if (renamed && changed) {
    clearTimeout(timer);
    watcher.close();
    fs.unlinkSync(path + '/hello');
    fs.rmdirSync(path);
    console.log('node-inotify-ok');
  }
});
fs.writeFileSync(path + '/hello', 'actual libuv inotify events\n');
