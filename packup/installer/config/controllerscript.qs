function Controller()
{
    if (installer.isInstaller()) {
        installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.Yes);
        installer.setMessageBoxAutomaticAnswer("MaintenanceToolFound", QMessageBox.Ok);

        installer.valueChanged.connect(this, this.onValueChanged);
        this.removeMaintenance(installer.value("TargetDir"));
    }

    if (installer.isUninstaller()) {
        installer.setDefaultPageVisible(QInstaller.Introduction, true);
        installer.setValue("removeSettingsButton", "true");
    }
}

Controller.prototype.IntroductionPageCallback = function()
{
    if (installer.isUninstaller()) {
        var widget = gui.currentPageWidget();
        if (widget) {
            var settingsButton = widget.findChild("SettingsButton");
            if (settingsButton) settingsButton.setVisible(false);
        }
        try { gui.setSettingsButtonEnabled(false); } catch(e) {}
    }
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    var dir = installer.value("TargetDir");
    this.removeMaintenance(dir);
    // Re-set TargetDir to trigger page isComplete() re-evaluation
    // so the Next button re-enables after maintenance tool removal
    installer.setValue("TargetDir", dir);
}

Controller.prototype.onValueChanged = function(key, value)
{
    if (key === "TargetDir") {
        this.removeMaintenance(value);
    }
}

Controller.prototype.removeMaintenance = function(dir)
{
    if (!dir || dir === "") return;
    try {
        if (systemInfo.kernelType === "winnt") {
            installer.execute("powershell.exe", ["-NoProfile", "-Command",
                "Remove-Item -Force -ErrorAction SilentlyContinue '" + dir + "/vlink-maintenance.exe'"]);
        } else {
            installer.execute("rm", ["-f", dir + "/vlink-maintenance"]);
        }
    } catch (e) {
        // Ignore
    }
}
