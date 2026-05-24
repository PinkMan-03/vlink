function bashEscape(s) {
    if (s === undefined || s === null) return "''";
    return "'" + ("" + s).replace(/'/g, "'\\''") + "'";
}

function normPath(p) {
    if (!p) return "";
    var s = ("" + p).replace(/[\r\n]+/g, "").replace(/\\/g, "/").trim();
    if (s.length > 1) s = s.replace(/\/+$/, "");
    return s;
}

function toWinPath(p) {
    return ("" + p).replace(/\//g, "\\");
}

var DANGEROUS_TARGETS = {
    winnt: [
        "c:", "c:/windows", "c:/windows/system32",
        "c:/program files", "c:/program files (x86)",
        "c:/users", "c:/users/public", "c:/programdata"
    ],
    darwin: [
        "/", "/applications", "/system", "/library",
        "/usr", "/usr/local", "/bin", "/sbin", "/etc", "/var",
        "/users", "/users/shared",
        "/opt", "/private", "/volumes"
    ],
    linux: [
        "/", "/home", "/root", "/boot", "/dev",
        "/etc", "/lib", "/lib64", "/proc", "/sys",
        "/opt", "/usr", "/usr/local", "/usr/bin", "/usr/lib", "/usr/share",
        "/var", "/srv", "/run", "/tmp", "/mnt", "/media", "/snap"
    ]
};

function isObviouslyDangerousTarget(targetDir) {
    if (!targetDir) return true;
    var n = normPath(targetDir);
    if (n === "") return true;
    var caseFold = (systemInfo.kernelType === "winnt" ||
                    systemInfo.kernelType === "darwin");
    var nKey = caseFold ? n.toLowerCase() : n;

    var list = DANGEROUS_TARGETS[systemInfo.kernelType] || DANGEROUS_TARGETS.linux;
    for (var i = 0; i < list.length; ++i) {
        if (nKey === list[i]) return true;
    }
    if (/^[a-z]:$/i.test(n)) return true;

    var homeVar = systemInfo.kernelType === "winnt" ? "USERPROFILE" : "HOME";
    var home = installer.environmentVariable(homeVar);
    if (home && home !== "") {
        var hn = normPath(home);
        var hnKey = caseFold ? hn.toLowerCase() : hn;
        if (hnKey === nKey) return true;
    }
    return false;
}

function hasPolkit() {
    try {
        var r = installer.execute("/bin/bash", ["-c",
            "command -v pkexec >/dev/null 2>&1 && echo yes || echo no"]);
        return r && r.length > 0 && (("" + r[0]).indexOf("yes") !== -1);
    } catch (e) {
        return false;
    }
}

function readOldInstallPath()
{
    try {
        if (systemInfo.kernelType === "winnt") {
            var r = installer.execute("reg",
                ["query", "HKCU\\Software\\VLink", "/v", "InstallPath"]);
            if (r && r.length > 1 && r[1] == 0) {
                var m = r[0].match(/InstallPath\s+REG_SZ\s+([^\r\n]+)/);
                if (m) return normPath(m[1]);
            }
        } else {
            var home = installer.environmentVariable("HOME");
            if (!home || home === "") return "";
            var r = installer.execute("/bin/cat",
                [home + "/.config/vlink/install_path"]);
            if (r && r.length > 1 && r[1] == 0) {
                return normPath(r[0]);
            }
        }
    } catch (e) {}
    return "";
}

function Component()
{
    if (installer.isInstaller()) {
        installer.installationStarted.connect(
            Component.prototype.onInstallationStarted.bind(this));
    }
}

function targetHasVlinkSignature(targetDir)
{
    if (!targetDir || targetDir === "") return false;
    try {
        if (systemInfo.kernelType === "winnt") {
            var tdWin = toWinPath(targetDir);
            var r = installer.execute("cmd.exe", ["/c",
                "if exist \"" + tdWin + "\\install.bat\" echo yes & " +
                "if exist \"" + tdWin + "\\uninstall.bat\" echo yes & " +
                "if exist \"" + tdWin + "\\bin\\vlink-viewer.exe\" echo yes & " +
                "if exist \"" + tdWin + "\\vlink-maintenance.exe\" echo yes & " +
                "if exist \"" + tdWin + "\\vlink-maintenance.dat\" echo yes & " +
                "exit /b 0"]);
            return r && r.length > 0 && (("" + r[0]).indexOf("yes") !== -1);
        }
        var tdQ = bashEscape(targetDir);
        var r2 = installer.execute("/bin/bash", ["-c",
            "if [ -f " + tdQ + "/install.sh ] || " +
            "   [ -f " + tdQ + "/uninstall.sh ] || " +
            "   [ -f " + tdQ + "/bin/vlink-viewer ] || " +
            "   [ -f " + tdQ + "/vlink-maintenance ] || " +
            "   [ -d " + tdQ + "/vlink-maintenance.app ]; then " +
            "  echo yes; " +
            "fi; exit 0"]);
        return r2 && r2.length > 0 && (("" + r2[0]).indexOf("yes") !== -1);
    } catch (e) {}
    return false;
}

function wipeTargetContents(targetDir)
{
    if (!targetDir || targetDir === "") return;
    try {
        if (systemInfo.kernelType === "winnt") {
            var tdWin = toWinPath(targetDir);
            installer.execute("cmd.exe", ["/c",
                "pushd \"" + tdWin + "\" >nul 2>&1 && " +
                "(del /f /q /a * >nul 2>&1) & " +
                "(for /d %d in (*) do rmdir /s /q \"%d\" >nul 2>&1) & " +
                "popd & exit /b 0"]);
        } else {
            var tdQ = bashEscape(targetDir);
            installer.execute("/bin/bash", ["-c",
                "if [ -d " + tdQ + " ]; then " +
                "  find " + tdQ + " -mindepth 1 -delete 2>/dev/null || " +
                "  rm -rf " + tdQ + "/* " + tdQ + "/.[!.]* 2>/dev/null; " +
                "fi; exit 0"]);
        }
    } catch (e) {}
}

Component.prototype.onInstallationStarted = function()
{
    var targetDir = installer.value("TargetDir");
    var oldPath   = readOldInstallPath();
    var winnt     = (systemInfo.kernelType === "winnt");

    if (oldPath !== "") {
        if (winnt) {
            var oldWin = toWinPath(oldPath);
            installer.execute("cmd.exe", ["/c",
                "if exist \"" + oldWin + "\\uninstall.bat\" " +
                "call \"" + oldWin + "\\uninstall.bat\""]);
            installer.execute("reg",
                ["delete", "HKCU\\Software\\VLink", "/f"]);
            installer.execute("reg", ["delete",
                "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLink",
                "/f"]);
        } else {
            var oldQ = bashEscape(oldPath);
            installer.execute("/bin/bash", ["-c",
                "if [ -x " + oldQ + "/uninstall.sh ]; then " +
                "  cd " + oldQ + " && ./uninstall.sh >/dev/null 2>&1 || true; " +
                "fi; exit 0"]);
            var home = installer.environmentVariable("HOME");
            if (home && home !== "") {
                var homeQ = bashEscape(home);
                installer.execute("/bin/bash", ["-c",
                    "rm -f " + homeQ + "/.config/vlink/install_path 2>/dev/null; " +
                    "rmdir " + homeQ + "/.config/vlink 2>/dev/null; exit 0"]);
            }
        }
    }

    if (targetHasVlinkSignature(targetDir)) {
        if (!winnt) {
            var tdQ2 = bashEscape(targetDir);
            installer.execute("/bin/bash", ["-c",
                "if [ -x " + tdQ2 + "/uninstall.sh ]; then " +
                "  cd " + tdQ2 + " && ./uninstall.sh >/dev/null 2>&1 || true; " +
                "fi; exit 0"]);
        } else {
            var tdWin2 = toWinPath(targetDir);
            installer.execute("cmd.exe", ["/c",
                "if exist \"" + tdWin2 + "\\uninstall.bat\" " +
                "call \"" + tdWin2 + "\\uninstall.bat\""]);
        }
        wipeTargetContents(targetDir);
    }
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    var targetDir = installer.value("TargetDir");

    if (isObviouslyDangerousTarget(targetDir)) {
        throw new Error("Refusing to install to TargetDir '" + targetDir +
                        "' (empty, filesystem root, drive root, or user home).");
    }

    if (systemInfo.kernelType === "winnt") {
        var targetWin = toWinPath(targetDir);

        component.addOperation("Execute",
            "reg", "add", "HKCU\\Software\\VLink",
            "/v", "InstallPath", "/t", "REG_SZ",
            "/d", targetWin, "/f",
            "UNDOEXECUTE",
            "cmd.exe", "/c",
            "reg delete \"HKCU\\Software\\VLink\" /f >nul 2>&1 & exit /b 0");

        component.addElevatedOperation("Execute",
            "cmd.exe", "/c", targetWin + "\\install.bat",
            "UNDOEXECUTE",
            "cmd.exe", "/c",
            "call \"" + targetWin + "\\uninstall.bat\" >nul 2>&1 & exit /b 0");

        var uk  = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLink";
        var ver = installer.value("ProductVersion");
        var mt  = targetWin + "\\vlink-maintenance.exe";
        var iconPath = targetWin + "\\bin\\vlink-viewer.exe,0";

        component.addOperation("Execute",
            "reg", "add", uk, "/v", "DisplayName",
            "/t", "REG_SZ", "/d", "VLink " + ver, "/f",
            "UNDOEXECUTE",
            "cmd.exe", "/c",
            "reg delete \"" + uk + "\" /f >nul 2>&1 & exit /b 0");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "DisplayVersion",
            "/t", "REG_SZ", "/d", ver, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "Publisher",
            "/t", "REG_SZ", "/d", "VLink", "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "InstallLocation",
            "/t", "REG_SZ", "/d", targetWin, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "UninstallString",
            "/t", "REG_SZ", "/d", mt, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "DisplayIcon",
            "/t", "REG_SZ", "/d", iconPath, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "NoModify",
            "/t", "REG_DWORD", "/d", "1", "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "NoRepair",
            "/t", "REG_DWORD", "/d", "1", "/f");

    } else {
        var home    = installer.environmentVariable("HOME");
        var targetQ = bashEscape(targetDir);
        var isMac   = (systemInfo.kernelType === "darwin");

        component.addOperation("Execute",
            "/bin/bash", "-c",
            "cd " + targetQ + " && " +
            "chmod +x install.sh uninstall.sh 2>/dev/null; " +
            "chmod +x bin/vlink-* bin/*.sh 2>/dev/null; " +
            "./install.sh",
            "UNDOEXECUTE",
            "/bin/bash", "-c",
            "if [ -x " + targetQ + "/uninstall.sh ]; then " +
            "  cd " + targetQ + " && ./uninstall.sh; " +
            "fi; exit 0");

        if (!isMac && hasPolkit()) {
            try {
                var check = installer.execute("/bin/bash", ["-c",
                    "grep -q Ubuntu /etc/os-release 2>/dev/null || exit 1; " +
                    "missing=''; for p in libvdpau-dev libva-drm2 libva-x11-2; do " +
                    "dpkg-query -W -f='${Status}' \"$p\" 2>/dev/null | grep -q 'install ok installed' || missing=\"$missing $p\"; done; " +
                    "[ -n \"$missing\" ] && echo $missing || exit 1"]);
                if (check && check.length > 1 && check[1] == 0 && check[0].trim() !== "") {
                    component.addElevatedOperation("Execute",
                        "/bin/bash", "-c",
                        "apt-get install -y " + check[0].trim() + " || true");
                }
            } catch (e) {
            }
        }

        if (home && home !== "") {
            var homeQ = bashEscape(home);
            component.addOperation("Execute",
                "/bin/bash", "-c",
                "mkdir -p " + homeQ + "/.config/vlink && " +
                "printf '%s\\n' " + targetQ + " > " + homeQ + "/.config/vlink/install_path",
                "UNDOEXECUTE",
                "/bin/bash", "-c",
                "rm -f " + homeQ + "/.config/vlink/install_path 2>/dev/null; " +
                "rmdir " + homeQ + "/.config/vlink 2>/dev/null; exit 0");
        }

        if (isMac) {
            component.addOperation("Execute",
                "/bin/bash", "-c",
                "ln -sfn vlink-maintenance.app " + targetQ + "/uninstall.app 2>/dev/null; exit 0",
                "UNDOEXECUTE",
                "/bin/bash", "-c",
                "rm -f " + targetQ + "/uninstall.app 2>/dev/null; exit 0");
        } else {
            component.addOperation("Execute",
                "/bin/bash", "-c",
                "ln -sfn vlink-maintenance " + targetQ + "/uninstall 2>/dev/null; exit 0",
                "UNDOEXECUTE",
                "/bin/bash", "-c",
                "rm -f " + targetQ + "/uninstall 2>/dev/null; exit 0");
        }
    }
}
