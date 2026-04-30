function Component()
{
    if (installer.isInstaller()) {
        var oldPath = this.readOldInstallPath();
        if (oldPath !== "") {
            if (systemInfo.kernelType === "winnt") {
                installer.execute("powershell.exe", ["-NoProfile", "-Command",
                    "Remove-Item -Force -ErrorAction SilentlyContinue '" + oldPath + "/vlink-maintenance.exe'"]);
            } else {
                installer.execute("rm", ["-f", oldPath + "/vlink-maintenance"]);
            }
        }
        installer.installationStarted.connect(this,
            Component.prototype.onInstallationStarted);
    }
}

Component.prototype.readOldInstallPath = function()
{
    try {
        if (systemInfo.kernelType === "winnt") {
            var result = installer.execute("reg",
                ["query", "HKCU\\Software\\VLink", "/v", "InstallPath"]);
            if (result.length > 1 && result[1] == 0) {
                var match = result[0].match(/InstallPath\s+REG_SZ\s+(.+)/);
                if (match) return match[1].trim();
            }
        } else {
            var home = installer.environmentVariable("HOME");
            var result = installer.execute("cat",
                [home + "/.config/vlink/install_path"]);
            if (result.length > 1 && result[1] == 0) {
                return result[0].trim();
            }
        }
    } catch (e) {}
    return "";
}

Component.prototype.onInstallationStarted = function()
{
    var oldPath = this.readOldInstallPath();
    if (oldPath === "") return;

    if (systemInfo.kernelType === "winnt") {
        installer.execute("cmd.exe",
            ["/c", oldPath + "/uninstall.bat"]);
        installer.execute("reg",
            ["delete", "HKCU\\Software\\VLink", "/f"]);
        installer.execute("reg", ["delete",
            "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLink", "/f"]);
        installer.execute("powershell.exe", ["-NoProfile", "-Command",
            "Remove-Item -Recurse -Force -ErrorAction SilentlyContinue '" + oldPath + "'"]);
    } else {
        installer.execute("bash",
            ["-c", "cd '" + oldPath + "' && ./uninstall.sh 2>/dev/null"]);
        var home = installer.environmentVariable("HOME");
        installer.execute("rm",
            ["-f", home + "/.config/vlink/install_path"]);
        installer.execute("rm", ["-rf", oldPath]);
    }
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    var targetDir = installer.value("TargetDir");

    if (systemInfo.kernelType === "winnt") {
        component.addOperation("Execute",
            "reg", "add", "HKCU\\Software\\VLink",
            "/v", "InstallPath", "/t", "REG_SZ",
            "/d", targetDir, "/f",
            "UNDOEXECUTE",
            "reg", "delete", "HKCU\\Software\\VLink", "/f");

        component.addElevatedOperation("Execute",
            "cmd.exe", "/c", targetDir + "/install.bat",
            "UNDOEXECUTE",
            "cmd.exe", "/c", targetDir + "/uninstall.bat");

        var uk = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLink";
        var ver = installer.value("ProductVersion");
        var mt = targetDir + "/vlink-maintenance.exe";

        component.addOperation("Execute",
            "reg", "add", uk, "/v", "DisplayName",
            "/t", "REG_SZ", "/d", "VLink " + ver, "/f",
            "UNDOEXECUTE",
            "reg", "delete", uk, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "DisplayVersion",
            "/t", "REG_SZ", "/d", ver, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "Publisher",
            "/t", "REG_SZ", "/d", "VLink", "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "InstallLocation",
            "/t", "REG_SZ", "/d", targetDir, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "UninstallString",
            "/t", "REG_SZ", "/d", mt, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "DisplayIcon",
            "/t", "REG_SZ", "/d", targetDir + "/bin/vlink-viewer.exe,0", "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "NoModify",
            "/t", "REG_DWORD", "/d", "1", "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "NoRepair",
            "/t", "REG_DWORD", "/d", "1", "/f");

    } else {
        var home = installer.environmentVariable("HOME");

        component.addOperation("Execute",
            "bash", "-c",
            "cd '" + targetDir + "' && chmod +x bin/vlink-* bin/*.sh bin/iox-* install.sh uninstall.sh 2>/dev/null && ./install.sh",
            "UNDOEXECUTE",
            "bash", "-c",
            "cd '" + targetDir + "' && ./uninstall.sh");

        if (systemInfo.productType !== "osx") {
            try {
                var check = installer.execute("bash", ["-c",
                    "grep -q Ubuntu /etc/os-release 2>/dev/null || exit 1; " +
                    "missing=''; for p in libvdpau-dev libva-drm2 libva-x11-2; do " +
                    "dpkg-query -W -f='${Status}' \"$p\" 2>/dev/null | grep -q 'install ok installed' || missing=\"$missing $p\"; done; " +
                    "[ -n \"$missing\" ] && echo $missing || exit 1"]);
                if (check.length > 1 && check[1] == 0 && check[0].trim() !== "") {
                    component.addElevatedOperation("Execute",
                        "bash", "-c", "apt-get install -y " + check[0].trim());
                }
            } catch (e) {
                // Skip silently
            }
        }

        component.addOperation("Execute",
            "bash", "-c",
            "mkdir -p '" + home + "/.config/vlink' && echo '" + targetDir + "' > '" + home + "/.config/vlink/install_path'",
            "UNDOEXECUTE",
            "bash", "-c",
            "rm -f '" + home + "/.config/vlink/install_path'");

        component.addOperation("Execute",
            "bash", "-c",
            "ln -sf vlink-maintenance '" + targetDir + "/uninstall'",
            "UNDOEXECUTE",
            "bash", "-c",
            "rm -f '" + targetDir + "/uninstall'");
    }
}
