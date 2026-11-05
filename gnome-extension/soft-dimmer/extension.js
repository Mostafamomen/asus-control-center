// SPDX-License-Identifier: GPL-3.0-or-later
//
// Soft Dimmer (ASUSCraft) — GNOME Shell extension.
// Minimal GJS adapter: D-Bus client → Clutter overlays + Quick Settings.
// The C++ engine in ASUSCraft owns all brightness state; this extension
// is a thin client that renders overlays and provides Quick Settings.

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {MonitorDimmer} from './dimmer.js';
import {SoftDimmerIndicator} from './indicator.js';
import {SoftDimmerDbusClient} from './dbus_client.js';

export default class SoftDimmerAscExtension extends Extension {
    enable() {
        // Create the D-Bus client and connect to ASUSCraft.
        this._dbus = new SoftDimmerDbusClient();

        // Try to connect (asynchronous, non-blocking).
        this._dbus.connect().then(available => {
            this._available = available;
            if (!available) {
                g_printerr('soft-dimmer-asc: ASUSCraft not running; '
                    + 'overlay will be inactive until ASUSCraft starts.');
                return;
            }

            // Create the dimmer (overlays) once D-Bus is available.
            this._dimmer = new MonitorDimmer(this._dbus);
            this._dimmer.enable();

            // Create the Quick Settings indicator.
            try {
                this._indicator = new SoftDimmerIndicator(this._dbus);
                Main.panel.statusArea.quickSettings.addExternalIndicator(
                    this._indicator);
            } catch (e) {
                g_printerr(`soft-dimmer-asc: indicator failed: ${e}`);
                this._indicator = null;
            }
        }).catch(e => {
            g_printerr(`soft-dimmer-asc: D-Bus connection failed: ${e}`);
            this._available = false;
        });
    }

    disable() {
        // Remove Quick Settings indicator first.
        if (this._indicator) {
            try {
                this._indicator.quickSettingsItems.forEach(item => item.destroy());
                this._indicator.destroy();
            } catch (e) {
                g_printerr(`soft-dimmer-asc: indicator cleanup failed: ${e}`);
            }
            this._indicator = null;
        }

        // Disable dimmer (removes all overlays).
        if (this._dimmer) {
            this._dimmer.disable();
            this._dimmer = null;
        }

        // Close D-Bus connection.
        if (this._dbus) {
            this._dbus.close();
            this._dbus = null;
        }

        this._available = false;
    }
}
