// The Windows installer of FielDes (and, built without the payload, its uninstaller).
//
// One program, two builds (see build-installer.ps1):
//   * Setup:       the portable folder as a zip resource ("payload.zip"), the uninstaller ("uninstall.exe") and "version.txt".
//                  It asks for the language and the folder, unpacks the payload, makes the shortcuts, the entry in
//                  Settings > Apps and the program's language setting.
//   * Uninstaller: the same code without a payload; it removes what the installation wrote (the list is in install.manifest).
//
// Command line (Setup and uninstaller):
//   /S                 silent: no window, the defaults and what follows
//   /language=xx       en, es, fr, de, ja, zh or ru (default: the language of Windows)
//   /dir=PATH          the install folder (default: %LOCALAPPDATA%\Programs\FielDes, or where FielDes is installed already)
//   /nodesktop         no desktop shortcut
//   /nolaunch          do not start FielDes when the installation is finished
//   /uninstall         (Setup) remove FielDes
//
// It installs for the current user only: it needs no administrator rights.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace FielDesSetup
{
    static class Texts
    {
        public static readonly string[] Codes = { "en", "es", "fr", "de", "ja", "zh", "ru" };
        // (each language's own name, as it is written in its own language)
        public static readonly string[] Names = { "English", "Español", "Français", "Deutsch", "日本語", "简体中文", "Русский" };

        // key -> the text in en, es, fr, de, ja, zh, ru
        static readonly Dictionary<string, string[]> table = new Dictionary<string, string[]>
        {
            { "title", new[] { "FielDes Setup", "Instalación de FielDes", "Installation de FielDes", "FielDes-Setup", "FielDes セットアップ", "安装 FielDes", "Установка FielDes" } },
            { "uninstall_title", new[] { "FielDes Uninstall", "Desinstalación de FielDes", "Désinstallation de FielDes", "FielDes deinstallieren", "FielDes のアンインストール", "卸载 FielDes", "Удаление FielDes" } },
            { "welcome", new[] {
                "FielDes will be installed for your user account. No administrator rights are needed.",
                "FielDes se instalará para tu cuenta de usuario. No se necesitan permisos de administrador.",
                "FielDes sera installé pour votre compte utilisateur. Aucun droit d'administrateur n'est nécessaire.",
                "FielDes wird für Ihr Benutzerkonto installiert. Administratorrechte sind nicht nötig.",
                "FielDes はお使いのユーザーアカウント用にインストールされます。管理者権限は必要ありません。",
                "FielDes 将安装到您的用户账户下，无需管理员权限。",
                "FielDes будет установлен для вашей учётной записи. Права администратора не нужны." } },
            { "language", new[] { "Language of the program", "Idioma del programa", "Langue du programme", "Sprache des Programms", "プログラムの言語", "程序语言", "Язык программы" } },
            { "folder", new[] { "Install folder", "Carpeta de instalación", "Dossier d'installation", "Installationsordner", "インストール先フォルダー", "安装文件夹", "Папка установки" } },
            { "browse", new[] { "Browse...", "Examinar...", "Parcourir...", "Durchsuchen...", "参照...", "浏览...", "Обзор..." } },
            { "desktop", new[] { "Create a desktop shortcut", "Crear un acceso directo en el escritorio", "Créer un raccourci sur le bureau", "Verknüpfung auf dem Desktop anlegen", "デスクトップにショートカットを作成する", "创建桌面快捷方式", "Создать ярлык на рабочем столе" } },
            { "launch", new[] { "Start FielDes when the installation is finished", "Iniciar FielDes al terminar la instalación", "Lancer FielDes à la fin de l'installation", "FielDes nach der Installation starten", "インストール完了後に FielDes を起動する", "安装完成后启动 FielDes", "Запустить FielDes после установки" } },
            { "install", new[] { "Install", "Instalar", "Installer", "Installieren", "インストール", "安装", "Установить" } },
            { "update", new[] { "Update", "Actualizar", "Mettre à jour", "Aktualisieren", "更新", "更新", "Обновить" } },
            { "cancel", new[] { "Cancel", "Cancelar", "Annuler", "Abbrechen", "キャンセル", "取消", "Отмена" } },
            { "finish", new[] { "Finish", "Finalizar", "Terminer", "Fertig", "完了", "完成", "Готово" } },
            { "remove", new[] { "Uninstall", "Desinstalar", "Désinstaller", "Deinstallieren", "アンインストール", "卸载", "Удалить" } },
            { "installing", new[] { "Installing...", "Instalando...", "Installation en cours...", "Installation läuft...", "インストール中...", "正在安装...", "Установка..." } },
            { "removing", new[] { "Removing...", "Eliminando...", "Suppression en cours...", "Wird entfernt...", "削除中...", "正在卸载...", "Удаление..." } },
            { "done", new[] { "FielDes is installed.", "FielDes está instalado.", "FielDes est installé.", "FielDes ist installiert.", "FielDes をインストールしました。", "FielDes 已安装。", "FielDes установлен." } },
            { "removed", new[] { "FielDes was removed.", "FielDes se ha eliminado.", "FielDes a été supprimé.", "FielDes wurde entfernt.", "FielDes を削除しました。", "FielDes 已卸载。", "FielDes удалён." } },
            { "remove_ask", new[] {
                "Remove FielDes from this computer?\nYour own scripts and the folders you made are not touched.",
                "¿Eliminar FielDes de este equipo?\nTus propios scripts y las carpetas que creaste no se tocan.",
                "Supprimer FielDes de cet ordinateur ?\nVos propres scripts et les dossiers que vous avez créés ne sont pas touchés.",
                "FielDes von diesem Computer entfernen?\nIhre eigenen Skripte und die von Ihnen angelegten Ordner bleiben unberührt.",
                "このコンピューターから FielDes を削除しますか？\nご自身のスクリプトや作成したフォルダーはそのまま残ります。",
                "要从这台电脑上卸载 FielDes 吗？\n您自己的脚本和您创建的文件夹不会被改动。",
                "Удалить FielDes с этого компьютера?\nВаши собственные скрипты и созданные вами папки не затрагиваются." } },
            { "close_first", new[] {
                "FielDes is running from this folder. Close it, then try again.",
                "FielDes se está ejecutando desde esta carpeta. Ciérralo y vuelve a intentarlo.",
                "FielDes s'exécute depuis ce dossier. Fermez-le, puis réessayez.",
                "FielDes läuft aus diesem Ordner. Schließen Sie es und versuchen Sie es erneut.",
                "FielDes がこのフォルダーから実行されています。終了してからもう一度お試しください。",
                "FielDes 正在从此文件夹运行。请先关闭它，然后重试。",
                "FielDes запущен из этой папки. Закройте его и повторите попытку." } },
            { "error", new[] { "The installation failed", "La instalación ha fallado", "L'installation a échoué", "Die Installation ist fehlgeschlagen", "インストールに失敗しました", "安装失败", "Не удалось установить" } },
            { "error_remove", new[] { "The removal failed", "La desinstalación ha fallado", "La désinstallation a échoué", "Die Deinstallation ist fehlgeschlagen", "アンインストールに失敗しました", "卸载失败", "Не удалось удалить" } },
            { "why_folder", new[] {
                "Choose a folder you can write to, for example one in your user folder.",
                "Elige una carpeta en la que puedas escribir, por ejemplo una de tu carpeta de usuario.",
                "Choisissez un dossier dans lequel vous pouvez écrire, par exemple un dossier de votre profil.",
                "Wählen Sie einen Ordner, in den Sie schreiben dürfen, zum Beispiel einen in Ihrem Benutzerordner.",
                "書き込み可能なフォルダー（たとえばユーザーフォルダー内）を選んでください。",
                "请选择一个可以写入的文件夹，例如用户文件夹中的某个文件夹。",
                "Выберите папку, в которую можно записывать, например папку в вашем профиле." } },
            { "app_name", new[] { "FielDes", "FielDes", "FielDes", "FielDes", "FielDes", "FielDes", "FielDes" } },
            { "tagline", new[] {
                "field-driven design", "diseño basado en campos", "conception par champs", "feldbasiertes Design",
                "フィールド駆動デザイン", "场驱动设计", "проектирование на основе полей" } },
        };

        public static int Index(string code)
        {
            int i = Array.IndexOf(Codes, code);
            return i < 0 ? 0 : i;
        }

        public static string Get(string key, string code)
        {
            string[] row;
            if (!table.TryGetValue(key, out row)) return key;
            return row[Index(code)];
        }

        // The language of Windows, if the program has it; English otherwise
        public static string SystemCode()
        {
            string two = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName.ToLowerInvariant();
            return Array.IndexOf(Codes, two) >= 0 ? two : "en";
        }
    }

    class Options
    {
        public bool Silent;
        public bool Uninstall;
        public bool NoDesktop;
        public bool NoLaunch;
        public string Language;
        public string Dir;
        public string RunFrom;      // (the uninstaller, started from a copy of itself: the folder it removes)

        public static Options Parse(string[] args)
        {
            Options o = new Options();
            foreach (string raw in args)
            {
                string a = raw.Trim();
                string lower = a.ToLowerInvariant();
                if (lower == "/s" || lower == "/silent" || lower == "-s") o.Silent = true;
                else if (lower == "/uninstall" || lower == "-uninstall") o.Uninstall = true;
                else if (lower == "/nodesktop") o.NoDesktop = true;
                else if (lower == "/nolaunch") o.NoLaunch = true;
                else if (lower.StartsWith("/language=")) o.Language = lower.Substring(10);
                else if (lower.StartsWith("/dir=")) o.Dir = a.Substring(5).Trim('"');
                else if (lower.StartsWith("/runfrom=")) o.RunFrom = a.Substring(9).Trim('"');
            }
            return o;
        }
    }

    static class Install
    {
        public const string Key = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\FielDes";
        public const string Manifest = "install.manifest";

        public static string DefaultDir()
        {
            string existing = Existing();
            if (existing != null) return existing;
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Programs\FielDes");
        }

        // Where FielDes is installed already (the entry in Settings > Apps), or null
        public static string Existing()
        {
            try
            {
                using (RegistryKey k = Registry.CurrentUser.OpenSubKey(Key))
                {
                    if (k == null) return null;
                    string dir = k.GetValue("InstallLocation") as string;
                    return !string.IsNullOrEmpty(dir) && Directory.Exists(dir) ? dir : null;
                }
            }
            catch { return null; }
        }

        public static string Version()
        {
            using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream("version.txt"))
            {
                if (s == null) return "";
                using (StreamReader r = new StreamReader(s)) return r.ReadToEnd().Trim();
            }
        }

        public static bool HasPayload()
        {
            return Assembly.GetExecutingAssembly().GetManifestResourceInfo("payload.zip") != null;
        }

        public static string StartMenuLink()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "FielDes.lnk");
        }

        public static string DesktopLink()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "FielDes.lnk");
        }

        // Is a FielDes running from this folder?
        public static bool Running(string dir)
        {
            string full = Path.GetFullPath(dir).TrimEnd('\\') + "\\";
            foreach (Process p in Process.GetProcessesByName("FielDes"))
            {
                try
                {
                    string exe = p.MainModule.FileName;
                    if (exe.StartsWith(full, StringComparison.OrdinalIgnoreCase)) return true;
                }
                catch { }
                finally { p.Dispose(); }
            }
            return false;
        }

        static void MakeLink(string link, string target, string workDir, string description)
        {
            Type shell = Type.GetTypeFromProgID("WScript.Shell");
            object sh = Activator.CreateInstance(shell);
            object lnk = shell.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, sh, new object[] { link });
            Type t = lnk.GetType();
            t.InvokeMember("TargetPath", BindingFlags.SetProperty, null, lnk, new object[] { target });
            t.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, lnk, new object[] { workDir });
            t.InvokeMember("Description", BindingFlags.SetProperty, null, lnk, new object[] { description });
            t.InvokeMember("IconLocation", BindingFlags.SetProperty, null, lnk, new object[] { target + ",0" });
            t.InvokeMember("Save", BindingFlags.InvokeMethod, null, lnk, null);
        }

        // Unpacks the payload into dir; progress is called with 0..100
        public static void Run(string dir, string language, bool desktop, Action<int> progress)
        {
            Directory.CreateDirectory(dir);
            string manifestPath = Path.Combine(dir, Manifest);

            // A previous installation: its files go (only the ones it wrote: what the user added stays)
            List<string> old = new List<string>();
            if (File.Exists(manifestPath)) old.AddRange(File.ReadAllLines(manifestPath, Encoding.UTF8));

            List<string> written = new List<string>();
            HashSet<string> writtenSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            Assembly self = Assembly.GetExecutingAssembly();
            string temp = Path.Combine(Path.GetTempPath(), "fieldes-payload-" + Guid.NewGuid().ToString("N") + ".zip");
            using (Stream src = self.GetManifestResourceStream("payload.zip"))
            using (FileStream dst = File.Create(temp)) src.CopyTo(dst);
            try
            {
                using (ZipArchive zip = ZipFile.OpenRead(temp))
                {
                    long total = 0, done = 0;
                    foreach (ZipArchiveEntry e in zip.Entries) total += e.Length;
                    if (total == 0) total = 1;
                    foreach (ZipArchiveEntry e in zip.Entries)
                    {
                        string rel = e.FullName.Replace('/', '\\');
                        if (rel.EndsWith("\\")) { Directory.CreateDirectory(Path.Combine(dir, rel)); continue; }
                        string path = Path.GetFullPath(Path.Combine(dir, rel));
                        if (!path.StartsWith(Path.GetFullPath(dir), StringComparison.OrdinalIgnoreCase)) continue;   // (never outside the folder)
                        Directory.CreateDirectory(Path.GetDirectoryName(path));
                        e.ExtractToFile(path, true);
                        written.Add(rel);
                        writtenSet.Add(rel);
                        done += e.Length;
                        progress((int)(done * 95 / total));
                    }
                }
            }
            finally { try { File.Delete(temp); } catch { } }

            // What the old version had and this one has not
            foreach (string rel in old)
            {
                if (rel.Length == 0 || writtenSet.Contains(rel) || rel == "uninstall.exe") continue;
                try { File.Delete(Path.Combine(dir, rel)); } catch { }
            }
            RemoveEmptyFolders(dir);

            // The uninstaller
            string uninstaller = Path.Combine(dir, "uninstall.exe");
            using (Stream src = self.GetManifestResourceStream("uninstall.exe"))
            using (FileStream dst = File.Create(uninstaller)) src.CopyTo(dst);
            written.Add("uninstall.exe");
            written.Add(Manifest);
            File.WriteAllLines(manifestPath, written.ToArray(), new UTF8Encoding(false));

            string exe = Path.Combine(dir, "FielDes.exe");
            string name = Texts.Get("app_name", language);
            string description = name + " - " + Texts.Get("tagline", language);
            Directory.CreateDirectory(Path.GetDirectoryName(StartMenuLink()));
            MakeLink(StartMenuLink(), exe, dir, description);
            if (desktop) MakeLink(DesktopLink(), exe, dir, description);
            else try { File.Delete(DesktopLink()); } catch { }

            // Settings > Apps
            using (RegistryKey k = Registry.CurrentUser.CreateSubKey(Key))
            {
                string version = Version();
                k.SetValue("DisplayName", "FielDes");
                k.SetValue("DisplayVersion", version);
                k.SetValue("Publisher", "FielDes");
                k.SetValue("InstallLocation", dir);
                k.SetValue("DisplayIcon", exe);
                k.SetValue("UninstallString", "\"" + uninstaller + "\"");
                k.SetValue("QuietUninstallString", "\"" + uninstaller + "\" /S");
                k.SetValue("NoModify", 1, RegistryValueKind.DWord);
                k.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                k.SetValue("EstimatedSize", (int)(DirSize(dir) / 1024), RegistryValueKind.DWord);
            }

            // The program's language: "system" follows Windows, a code is that language whatever Windows speaks
            using (RegistryKey k = Registry.CurrentUser.CreateSubKey(@"Software\FielDes\FielDes"))
                k.SetValue("language", language == Texts.SystemCode() ? "system" : language);

            progress(100);
        }

        static long DirSize(string dir)
        {
            long n = 0;
            foreach (string f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
                try { n += new FileInfo(f).Length; } catch { }
            return n;
        }

        // A file that is still in use for a moment (the program that started this one is closing) is tried again a few times
        static void DeleteFile(string path)
        {
            for (int tries = 0; tries < 20; tries++)
            {
                try { File.Delete(path); return; }
                catch (IOException) { Thread.Sleep(250); }
                catch (UnauthorizedAccessException) { Thread.Sleep(250); }
            }
        }

        static void RemoveEmptyFolders(string dir)
        {
            foreach (string d in Directory.GetDirectories(dir))
            {
                RemoveEmptyFolders(d);
                try { if (Directory.GetFileSystemEntries(d).Length == 0) Directory.Delete(d); } catch { }
            }
        }

        // Removes what Run wrote (not the folder of a user who put his own files in it)
        public static void Remove(string dir, Action<int> progress)
        {
            string manifestPath = Path.Combine(dir, Manifest);
            string[] files = File.Exists(manifestPath) ? File.ReadAllLines(manifestPath, Encoding.UTF8) : new string[0];
            for (int i = 0; i < files.Length; i++)
            {
                if (files[i].Length > 0) DeleteFile(Path.Combine(dir, files[i]));
                progress((int)((i + 1) * 90L / Math.Max(1, files.Length)));
            }
            // (the uninstaller in the folder was running a moment ago: it is deleted last, and given a few tries)
            DeleteFile(manifestPath);
            RemoveEmptyFolders(dir);
            try { if (Directory.GetFileSystemEntries(dir).Length == 0) Directory.Delete(dir); } catch { }
            try { File.Delete(StartMenuLink()); } catch { }
            try { File.Delete(DesktopLink()); } catch { }
            try { Registry.CurrentUser.DeleteSubKeyTree(Key); } catch { }
            progress(100);
        }
    }

    class SetupForm : Form
    {
        readonly Options options;
        readonly bool uninstalling;
        readonly string installed;          // (where FielDes is already installed, or null)
        string language;
        Label welcome, languageLabel, folderLabel, status;
        ComboBox languageBox;
        TextBox folderBox;
        Button browse, go, cancel;
        CheckBox desktop, launch;
        ProgressBar bar;
        bool finished;

        public SetupForm(Options o, bool uninstall, string dir)
        {
            options = o;
            uninstalling = uninstall;
            installed = Install.Existing();
            language = o.Language != null && Array.IndexOf(Texts.Codes, o.Language) >= 0 ? o.Language : Texts.SystemCode();

            Font = SystemFonts.MessageBoxFont;
            AutoScaleDimensions = new SizeF(96F, 96F);          // (the sizes below are in pixels at 96 dpi)
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(560, uninstall ? 170 : 300);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            welcome = new Label { Left = 16, Top = 14, Width = 528, Height = 40 };
            Controls.Add(welcome);

            if (!uninstall)
            {
                languageLabel = new Label { Left = 16, Top = 66, Width = 528, Height = 18 };
                languageBox = new ComboBox { Left = 16, Top = 86, Width = 240, DropDownStyle = ComboBoxStyle.DropDownList };
                foreach (string n in Texts.Names) languageBox.Items.Add(n);
                languageBox.SelectedIndex = Texts.Index(language);
                languageBox.SelectedIndexChanged += delegate { language = Texts.Codes[languageBox.SelectedIndex]; Translate(); };

                folderLabel = new Label { Left = 16, Top = 120, Width = 528, Height = 18 };
                folderBox = new TextBox { Left = 16, Top = 140, Width = 432, Text = dir };
                browse = new Button { Left = 456, Top = 138, Width = 88, Height = 26 };
                browse.Click += delegate
                {
                    using (FolderBrowserDialog d = new FolderBrowserDialog())
                    {
                        d.SelectedPath = folderBox.Text;
                        if (d.ShowDialog(this) == DialogResult.OK)
                            folderBox.Text = Path.GetFileName(d.SelectedPath).Equals("FielDes", StringComparison.OrdinalIgnoreCase)
                                ? d.SelectedPath : Path.Combine(d.SelectedPath, "FielDes");
                    }
                };
                desktop = new CheckBox { Left = 16, Top = 174, Width = 528, Height = 22, Checked = true };
                launch = new CheckBox { Left = 16, Top = 198, Width = 528, Height = 22, Checked = true };
                Controls.AddRange(new Control[] { languageLabel, languageBox, folderLabel, folderBox, browse, desktop, launch });
            }

            bar = new ProgressBar { Left = 16, Top = ClientSize.Height - 82, Width = 528, Height = 14, Visible = false };
            status = new Label { Left = 16, Top = ClientSize.Height - 62, Width = 528, Height = 20 };
            go = new Button { Left = ClientSize.Width - 200, Top = ClientSize.Height - 38, Width = 90, Height = 28 };
            cancel = new Button { Left = ClientSize.Width - 104, Top = ClientSize.Height - 38, Width = 88, Height = 28 };
            go.Click += OnGo;
            cancel.Click += delegate { Close(); };
            Controls.AddRange(new Control[] { bar, status, go, cancel });
            AcceptButton = go;
            Translate();
        }

        void Translate()
        {
            string c = language;
            Text = Texts.Get(uninstalling ? "uninstall_title" : "title", c);
            welcome.Text = uninstalling ? Texts.Get("remove_ask", c) : Texts.Get("welcome", c);
            if (!uninstalling)
            {
                languageLabel.Text = Texts.Get("language", c);
                folderLabel.Text = Texts.Get("folder", c);
                browse.Text = Texts.Get("browse", c);
                desktop.Text = Texts.Get("desktop", c);
                launch.Text = Texts.Get("launch", c);
            }
            go.Text = finished ? Texts.Get("finish", c) : Texts.Get(uninstalling ? "remove" : (installed != null ? "update" : "install"), c);
            cancel.Text = Texts.Get("cancel", c);
            cancel.Visible = !finished;
            // (CJK text needs a font that has it: the message box font of the system language does, others fall back by themselves)
        }

        void SetEnabled(bool on)
        {
            go.Enabled = on;
            if (!uninstalling) { languageBox.Enabled = on; folderBox.Enabled = on; browse.Enabled = on; desktop.Enabled = on; launch.Enabled = on; }
        }

        void OnGo(object sender, EventArgs e)
        {
            if (finished)
            {
                Close();
                return;
            }
            string dir = uninstalling ? options.RunFrom : folderBox.Text.Trim();
            if (string.IsNullOrEmpty(dir)) return;
            if (Install.Running(dir))
            {
                MessageBox.Show(this, Texts.Get("close_first", language), Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            SetEnabled(false);
            bar.Visible = true;
            status.Text = Texts.Get(uninstalling ? "removing" : "installing", language);
            bool wantDesktop = !uninstalling && desktop.Checked;
            bool wantLaunch = !uninstalling && launch.Checked;
            string lang = language;
            Thread worker = new Thread(delegate()
            {
                Exception failure = null;
                try
                {
                    Action<int> progress = delegate(int p) { BeginInvoke((Action)delegate { bar.Value = Math.Min(100, p); }); };
                    if (uninstalling) Install.Remove(dir, progress); else Install.Run(dir, lang, wantDesktop, progress);
                }
                catch (Exception ex) { failure = ex; }
                BeginInvoke((Action)delegate { Finished(failure, dir, wantLaunch); });
            });
            worker.IsBackground = true;
            worker.Start();
        }

        void Finished(Exception failure, string dir, bool wantLaunch)
        {
            if (failure != null)
            {
                // (the reason, and what to do about it: a folder that cannot be written is the usual one)
                MessageBox.Show(this, Texts.Get(uninstalling ? "error_remove" : "error", language) + ":\n\n" + failure.Message
                    + (failure is UnauthorizedAccessException ? "\n\n" + Texts.Get("why_folder", language) : ""),
                    Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                bar.Visible = false;
                status.Text = "";
                SetEnabled(true);
                return;
            }
            finished = true;
            bar.Value = 100;
            status.Text = Texts.Get(uninstalling ? "removed" : "done", language);
            go.Enabled = true;
            Translate();
            if (wantLaunch) Launch(dir);
        }

        public static void Launch(string dir)
        {
            try { Process.Start(new ProcessStartInfo(Path.Combine(dir, "FielDes.exe")) { WorkingDirectory = dir }); } catch { }
        }
    }

    static class Program
    {
        // (without this Windows stretches the window as a picture on a high-resolution screen: blurry)
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool SetProcessDPIAware();

        [STAThread]
        static int Main(string[] args)
        {
            Options o = Options.Parse(args);
            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            bool hasPayload = Install.HasPayload();
            try
            {
                if (hasPayload && !o.Uninstall) return Setup(o);
                return Remove(o);
            }
            catch (Exception ex)
            {
                if (!o.Silent) MessageBox.Show(ex.Message, "FielDes", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        static int Setup(Options o)
        {
            string dir = o.Dir != null ? o.Dir : Install.DefaultDir();
            string language = o.Language != null && Array.IndexOf(Texts.Codes, o.Language) >= 0 ? o.Language : Texts.SystemCode();
            if (o.Silent)
            {
                if (Install.Running(dir)) return 2;
                Install.Run(dir, language, !o.NoDesktop, delegate(int p) { });
                if (!o.NoLaunch) SetupForm.Launch(dir);
                return 0;
            }
            Application.Run(new SetupForm(o, false, dir));
            return 0;
        }

        // The uninstaller (or Setup /uninstall): a program that sits in the folder it removes copies itself to the temp folder first
        static int Remove(Options o)
        {
            string dir = o.RunFrom != null ? o.RunFrom : (o.Dir != null ? o.Dir : Install.Existing());
            if (dir == null) return 0;
            string me = Application.ExecutablePath;
            string full = Path.GetFullPath(dir).TrimEnd('\\') + "\\";
            if (o.RunFrom == null && Path.GetFullPath(me).StartsWith(full, StringComparison.OrdinalIgnoreCase))
            {
                string copy = Path.Combine(Path.GetTempPath(), "fieldes-uninstall-" + Guid.NewGuid().ToString("N") + ".exe");
                File.Copy(me, copy, true);
                StringBuilder a = new StringBuilder();
                a.Append("/runfrom=\"" + dir + "\"");
                if (o.Silent) a.Append(" /S");
                if (o.Language != null) a.Append(" /language=" + o.Language);
                Process.Start(new ProcessStartInfo(copy, a.ToString()) { UseShellExecute = false });
                return 0;
            }
            o.RunFrom = dir;
            if (o.Silent)
            {
                if (Install.Running(dir)) return 2;
                Install.Remove(dir, delegate(int p) { });
                CleanUp(me);
                return 0;
            }
            Application.Run(new SetupForm(o, true, dir));
            CleanUp(me);
            return 0;
        }

        // The temporary copy of the uninstaller removes itself once it has exited
        static void CleanUp(string me)
        {
            if (!Path.GetFileName(me).StartsWith("fieldes-uninstall-", StringComparison.OrdinalIgnoreCase)) return;
            try
            {
                ProcessStartInfo p = new ProcessStartInfo("cmd.exe", "/c ping -n 3 127.0.0.1 >nul & del /f /q \"" + me + "\"");
                p.CreateNoWindow = true;
                p.UseShellExecute = false;
                Process.Start(p);
            }
            catch { }
        }
    }
}
