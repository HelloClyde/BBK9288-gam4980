"""Test the production host catch-up boundary, including clock wrap."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_direct_framebuffer import function_source, SOURCE

class BatchPolicy(unittest.TestCase):
    def test_boundaries(self):
        body=function_source(SOURCE.read_text(encoding='utf-8'),'bare_batch_should_yield')
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'batch.c';exe=Path(temp)/'batch.exe'
            source.write_text('#include <stdint.h>\n#include <assert.h>\ntypedef uint32_t u32;\n'+body+'''
int main(void) {
 assert(!bare_batch_should_yield(100,107,1,4));
 assert(bare_batch_should_yield(100,108,1,4));
 assert(bare_batch_should_yield(100,200,3,4));
 assert(!bare_batch_should_yield(100,200,4,4));
 assert(!bare_batch_should_yield(100,200,1,1));
 assert(bare_batch_should_yield(0xfffffffc,4,1,4));
 assert(!bare_batch_should_yield(0xfffffffc,3,1,4));
 return 0;
}
''',encoding='utf-8')
            subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__':unittest.main()
