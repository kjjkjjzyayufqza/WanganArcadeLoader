param()

$ErrorActionPreference = 'Stop'

# Monitor outside the target process so hard termination still leaves an exit code.
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

public static class LoaderRuntimeMonitor
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string name, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteFile(SafeFileHandle file, byte[] data, uint size,
        out uint written, IntPtr overlapped);
    private static readonly object Gate = new object();
    private static SafeFileHandle LogFile;

    private static string Redact(string text)
    {
        text = Regex.Replace(text, @"(?i)\b[a-z]:[\\/][^\r\n\""'<>\t]+", "<local-path>");
        text = Regex.Replace(text, @"(?i)(access_?code|chip_?id|pcb_serial|password|api_key|token|cookie|authorization)\s*[:=]\s*[^\s,;]+", "$1=<redacted>");
        text = Regex.Replace(text, @"\b\d{12,}\b", "<redacted-id>");
        return Regex.Replace(text, @"[\w.+%-]+@[\w.-]+\.[A-Za-z]{2,}", "<redacted-email>");
    }

    private static void Write(string source, string message)
    {
        if (String.IsNullOrEmpty(message)) return;
        byte[] data = Encoding.UTF8.GetBytes(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") +
            " [" + source + "] " + Redact(message) + "\r\n");
        lock (Gate)
        {
            uint written;
            if (!WriteFile(LogFile, data, (uint)data.Length, out written, IntPtr.Zero) || written != data.Length)
                throw new IOException("Cannot append runtime diagnostics.");
        }
    }

    public static int Run(string directory)
    {
        // FILE_APPEND_DATA and shared access match the native logger. A normal
        // append stream can overwrite concurrent native writes on Windows.
        using (LogFile = CreateFile(Path.Combine(directory, "WAL.log"), 4, 7,
            IntPtr.Zero, 4, 0x80, IntPtr.Zero))
        {
            if (LogFile.IsInvalid) throw new IOException("Cannot open WAL.log.");
            using (Process child = new Process())
            {
                child.StartInfo = new ProcessStartInfo(Path.Combine(directory, "wmn6r.exe"));
                child.StartInfo.WorkingDirectory = directory;
                child.StartInfo.UseShellExecute = false;
                child.StartInfo.CreateNoWindow = true;
                child.StartInfo.RedirectStandardOutput = true;
                child.StartInfo.RedirectStandardError = true;
                child.OutputDataReceived += delegate(object sender, DataReceivedEventArgs args) { Write("Stdout", args.Data); };
                child.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs args) { Write("Stderr", args.Data); };
                Write("Launcher", "Session launch requested; stdout, stderr and exit monitoring enabled");
                child.Start();
                Write("Launcher", "Process started pid=" + child.Id);
                child.BeginOutputReadLine();
                child.BeginErrorReadLine();
                child.WaitForExit();
                int code = child.ExitCode;
                Write("Launcher", "Process exited code=" + code + " status=0x" + unchecked((uint)code).ToString("X8"));
                return code;
            }
        }
    }
}
'@

try {
    exit [LoaderRuntimeMonitor]::Run($PSScriptRoot)
} catch {
    [Console]::Error.WriteLine('Loader monitor failed. Check local files and permissions; private details were suppressed.')
    exit 1
}
