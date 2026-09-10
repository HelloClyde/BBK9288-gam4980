"""Build a small Windows GUI launcher using the already installed .NET CSC."""
from pathlib import Path
import os
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    if os.name!='nt': raise SystemExit('Windows only; run GAM-to-9288.pyw with Python on other platforms.')
    windows=Path(os.environ.get('WINDIR','C:/Windows'))
    candidates=[windows/'Microsoft.NET'/arch/'v4.0.30319/csc.exe' for arch in ('Framework64','Framework')]
    compiler=next((p for p in candidates if p.is_file()),None)
    if not compiler:raise SystemExit('Missing .NET Framework C# compiler')
    python=Path(sys.executable).with_name('pythonw.exe')
    if not python.is_file():raise SystemExit('Missing pythonw.exe')
    work=ROOT/'build/converter-launcher';work.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'tools/converter_launcher.cs').read_text(encoding='utf-8')
    source=source.replace('__PYTHONW__',str(python).replace('"','""'))
    (work/'Launcher.cs').write_text(source,encoding='utf-8')
    output=ROOT/'build/GAM-to-9288-Converter.exe'
    subprocess.run([str(compiler),'/nologo','/target:winexe','/codepage:65001',
        '/r:System.Windows.Forms.dll','/out:'+str(output),str(work/'Launcher.cs')],check=True)
    print(output)

if __name__=='__main__':main()
