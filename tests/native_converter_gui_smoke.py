"""Own-widget integration test; optional real compile, no OS input automation.

File dialogs are mocked here. This does not claim to test the Windows picker UI.
"""
import argparse
import ctypes
import json
from pathlib import Path
import sys
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import gam_converter_gui as gui


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--compile',action='store_true')
    args=parser.parse_args()
    if sys.platform=='win32':ctypes.windll.shcore.SetProcessDpiAwareness(1)
    root=gui.tk.Tk();app=gui.ConverterApp(root)
    errors=[]
    root.report_callback_exception=lambda *exc:errors.append(str(exc))
    artifact=ROOT/'build/converter-gui-test.json'
    result={}
    try:
        root.update()
        with patch.object(gui.filedialog,'askopenfilename',return_value=str(args.game)):
            app.choose_game()
        app.name.set('魔塔原生')
        assert Path(app.output.get()).stem=='魔塔原生'
        with patch.object(gui.filedialog,'askopenfilename',return_value=str(ROOT/'assets/9288/gam4980-icon-imagegen-v3.png')):
            app.choose_icon()
        with patch.object(gui.filedialog,'asksaveasfilename',return_value=str(ROOT/'build/converted/魔塔原生.exe')):
            app.choose_output()
        root.update()
        assert app.preview_name.cget('text')=='魔塔原生'
        assert app.icon_path is not None
        assert str(app.convert_button.cget('state'))=='normal'
        result['selection_name_icon_output']=True
        app.toggle_environment();root.update()
        assert app.env_window.state()!='withdrawn'
        result['expanded_window']=[root.winfo_width(),root.winfo_height()]
        result['expanded_convert_bottom']=app.convert_button.winfo_rooty()-root.winfo_rooty()+app.convert_button.winfo_height()
        assert result['expanded_convert_bottom'] < root.winfo_height(), result
        app.toggle_environment();root.update()
        result['collapsed_log_height']=app.log.winfo_height()
        assert app.log.winfo_height()>=50, result
        if args.compile:
            with patch.object(gui.messagebox,'askyesno',return_value=True):app.convert_button.invoke()
            assert app.running
            assert str(app.cancel_button.cget('state'))=='normal'
            assert all(str(w.cget('state'))=='disabled' for w in app.inputs)
            end=time.monotonic()+600
            while app.running and time.monotonic()<end:
                root.update();time.sleep(.04)
            assert not app.running, 'compile timed out'
            assert not errors, errors
            assert app.last_output, app.detail.cget('text')
            result['output']=str(app.last_output)
            result['status']=app.status.cget('text')
            result['log']=str(app.log_path)
            assert str(app.folder_button.cget('state'))=='normal'
            assert str(app.inputs[1].cget('state'))=='readonly'
        artifact.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)
    finally:
        if app.running:app.worker.cancel()
        root.destroy()


if __name__=='__main__':main()
