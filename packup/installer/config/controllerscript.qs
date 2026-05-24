function Controller()
{
    if (installer.isInstaller()) {
        installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.Yes);
        installer.setMessageBoxAutomaticAnswer("TargetDirectoryInUse",     QMessageBox.Yes);
        installer.setMessageBoxAutomaticAnswer("MaintenanceToolFound",     QMessageBox.Ok);
        installer.setMessageBoxAutomaticAnswer("stopProcessesForUpdates",  QMessageBox.Ignore);
        installer.valueChanged.connect(this.onValueChanged.bind(this));
        try { this.removeMaintenance(installer.value("TargetDir")); } catch (e) {}
    }

    if (installer.isUninstaller()) {
        installer.setDefaultPageVisible(QInstaller.Introduction,  true);
        installer.setDefaultPageVisible(QInstaller.TargetDirectory,  false);
        installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
        installer.setDefaultPageVisible(QInstaller.LicenseCheck,   false);
        installer.setDefaultPageVisible(QInstaller.StartMenuSelection, false);
        installer.setDefaultPageVisible(QInstaller.ReadyForInstallation, true);
        try { gui.setSettingsButtonEnabled(false); } catch (e) {}
    }
}

var DANGEROUS_DIRS = {
    winnt: [
        "c:", "c:/", "c:/windows", "c:/windows/system32",
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

function _norm(p) {
    if (!p) return "";
    var s = ("" + p).replace(/\\/g, "/");
    if (s.length > 1) s = s.replace(/\/+$/, "");
    if (systemInfo.kernelType === "winnt")  s = s.toLowerCase();
    if (systemInfo.kernelType === "darwin") s = s.toLowerCase();
    return s;
}

function _isDangerousTarget(dir) {
    if (!dir) return false;
    var n = _norm(dir);
    if (n === "") return false;

    var home = _norm(installer.value("HomeDir") ||
                     installer.environmentVariable(
                         systemInfo.kernelType === "winnt" ? "USERPROFILE" : "HOME"));
    if (home && n === home) return true;

    var list = DANGEROUS_DIRS[systemInfo.kernelType] || DANGEROUS_DIRS.linux;
    for (var i = 0; i < list.length; ++i) {
        if (n === list[i]) return true;
    }
    return false;
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    var dir = installer.value("TargetDir");

    if (_isDangerousTarget(dir)) {
        try {
            QMessageBox.warning("vlink.targetdir.dangerous",
                "VLink Installer",
                "The selected install directory '" + dir + "' is too dangerous " +
                "to use (uninstall would wipe this directory).\n\n" +
                "Please pick a dedicated subdirectory such as '" + dir + "/vlink'.",
                QMessageBox.Ok);
        } catch (e) {}
        var suggest = ("" + dir).replace(/[\/\\]+$/, "") +
                      (systemInfo.kernelType === "winnt" ? "\\vlink" : "/vlink");
        this._setTargetDirForced(suggest);
        return;
    }

    this.removeMaintenance(dir);
    this._setTargetDirForced(dir);
}

Controller.prototype.onValueChanged = function(key, value)
{
    if (key !== "TargetDir") return;
    if (this._inRevalidate) return;
    this.removeMaintenance(value);
    this._setTargetDirForced(value);
}

Controller.prototype._setTargetDirForced = function(dir)
{
    if (!dir || dir === "") return;
    if (this._inRevalidate) return;
    this._inRevalidate = true;
    try {
        installer.setValue("TargetDir", dir + ".vlinktmp");
        installer.setValue("TargetDir", dir);
    } catch (e) {}
    this._inRevalidate = false;
}

Controller.prototype.removeMaintenance = function(dir)
{
    if (!dir || dir === "") return;
    var path = ("" + dir).replace(/\\/g, "/").replace(/\/+$/, "");
    if (path === "") return;

    try {
        if (systemInfo.kernelType === "winnt") {
            var win = path.replace(/\//g, "\\");
            installer.execute("cmd.exe",
                ["/c", "if exist \"" + win + "\\vlink-maintenance.exe\" " +
                       "del /f /q \"" + win + "\\vlink-maintenance.exe\""]);
            installer.execute("cmd.exe",
                ["/c", "if exist \"" + win + "\\vlink-maintenance.dat\" " +
                       "del /f /q \"" + win + "\\vlink-maintenance.dat\""]);
        } else if (systemInfo.kernelType === "darwin") {
            installer.execute("/bin/rm", ["-rf", path + "/vlink-maintenance.app"]);
            installer.execute("/bin/rm", ["-f",  path + "/vlink-maintenance"]);
            installer.execute("/bin/rm", ["-f",  path + "/vlink-maintenance.dat"]);
            installer.execute("/bin/rm", ["-f",  path + "/uninstall.app"]);
        } else {
            installer.execute("/bin/rm", ["-f",  path + "/vlink-maintenance"]);
            installer.execute("/bin/rm", ["-f",  path + "/vlink-maintenance.dat"]);
            installer.execute("/bin/rm", ["-f",  path + "/uninstall"]);
        }
    } catch (e) {
    }
}
