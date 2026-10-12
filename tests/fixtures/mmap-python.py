# SPDX-License-Identifier: GPL-3.0-or-later
import os, mmap, tempfile, sqlite3
with tempfile.TemporaryFile() as f:
    f.truncate(8192)
    with mmap.mmap(f.fileno(),8192) as mapping:
        mapping[:4]=b'abcd'
        assert os.pread(f.fileno(),4,0)==b'abcd'
        os.pwrite(f.fileno(),b'XY',2)
        assert mapping[:4]==b'abXY'
        child=os.fork()
        if child==0:
            mapping[0:1]=b'Z'
            os._exit(0)
        _, status=os.waitpid(child,0)
        assert status==0 and mapping[:4]==b'ZbXY'
        mapping.flush()
        mapping.resize(16384)
        assert mapping[:4]==b'ZbXY' and mapping[12000]==0
with sqlite3.connect('/tmp/mmap-test.sqlite') as db:
    db.execute('pragma mmap_size=1048576')
    db.execute('create table if not exists data (n integer, value text)')
    db.execute('delete from data')
    db.executemany('insert into data values (?,?)', [(x,str(x)) for x in range(100)])
    db.commit()
    assert db.execute('select sum(n) from data').fetchone()[0]==4950
print('python-mmap-sqlite-ok')
