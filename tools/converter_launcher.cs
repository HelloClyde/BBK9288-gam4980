// Local desktop launcher; compiler/runtime dependencies stay in the project.
using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

class ConverterLauncher {
    private const string Python = @"__PYTHONW__";
    [STAThread]
    static void Main() {
        try {
            DirectoryInfo root = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
            while (root != null && !File.Exists(Path.Combine(root.FullName, "GAM-to-9288.pyw")))
                root = root.Parent;
            if (root == null) throw new Exception("请把转换器保留在工程的 build 目录中，不要单独移动这个启动 EXE。");
            if (!File.Exists(Python)) throw new Exception("找不到 Python 运行环境：" + Python + "\n请重新生成本机启动器。");
            ProcessStartInfo info = new ProcessStartInfo(Python);
            info.Arguments = "\"" + Path.Combine(root.FullName,"GAM-to-9288.pyw") + "\"";
            info.WorkingDirectory = root.FullName;
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            Process.Start(info);
        } catch (Exception error) {
            MessageBox.Show(error.Message,"GAM → 9288 转换器",MessageBoxButtons.OK,MessageBoxIcon.Error);
        }
    }
}
