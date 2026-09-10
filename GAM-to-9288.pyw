"""Double-click entry point. The EXE launcher invokes this with pythonw."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'tools'))
try:
    from gam_converter_gui import main
    main()
except Exception:
    import traceback
    import tkinter.messagebox
    error=traceback.format_exc()
    log=Path(__file__).resolve().parent/'build/converter-startup.log'
    log.parent.mkdir(parents=True,exist_ok=True);log.write_text(error,encoding='utf-8')
    tkinter.messagebox.showerror('转换器启动失败',error+'\n\n日志：'+str(log))
    raise
