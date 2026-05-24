function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    var targetDir = installer.value("TargetDir");

    if (systemInfo.kernelType === "winnt") {
        var targetWin = targetDir.replace(/\//g, "\\");
        var uk        = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLink";
        var ver       = installer.value("ProductVersion");
        var maint     = targetWin + "\\vlink-uninstall.exe";
        var iconPath  = targetWin + "\\bin\\vlink-viewer.exe,0";

        component.addElevatedOperation("Execute",
            "cmd.exe", "/c", targetDir + "/install.bat",
            "UNDOEXECUTE",
            "cmd.exe", "/c", targetDir + "/uninstall.bat");

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
            "/t", "REG_SZ", "/d", targetWin, "/f");
        component.addOperation("Execute",
            "reg", "add", uk, "/v", "UninstallString",
            "/t", "REG_SZ", "/d", maint, "/f");
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
        component.addOperation("Execute",
            "bash", "-c",
            "cd '" + targetDir + "' && chmod +x install.sh uninstall.sh bin/vlink-* bin/*.sh 2>/dev/null && ./install.sh",
            "UNDOEXECUTE",
            "bash", "-c",
            "cd '" + targetDir + "' && ./uninstall.sh");
    }
};
